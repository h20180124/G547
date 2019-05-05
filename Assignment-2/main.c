#include <linux/string.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/genhd.h> // For basic block driver framework
#include <linux/blkdev.h> // For at least, struct block_device_operations
#include <linux/hdreg.h> // For struct hd_geometry
#include <linux/errno.h>

//#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*a))

#define SECTOR_SIZE 512
#define MBR_SIZE SECTOR_SIZE
#define MBR_DISK_SIGNATURE_OFFSET 440
#define MBR_DISK_SIGNATURE_SIZE 4
#define PARTITION_TABLE_OFFSET 446
#define PARTITION_ENTRY_SIZE 16 // sizeof(PartEntry)
#define PARTITION_TABLE_SIZE 32 // sizeof(PartTable)
#define MBR_SIGNATURE_OFFSET 510
#define MBR_SIGNATURE_SIZE 2
#define MBR_SIGNATURE 0xAA55
#define BR_SIZE SECTOR_SIZE
#define BR_SIGNATURE_OFFSET 510
#define BR_SIGNATURE_SIZE 2
#define BR_SIGNATURE 0xAA55
#define DOF_DEVICE_SIZE 1024
#define DOF_SECTOR_SIZE 512
#define DOF_FIRST_MINOR 0
#define DOF_MINOR_CNT 5

static u8 *dev_data;
static u_int dof_major = 0;

void copy_mbr(u8 *disk);
int ramdevice_init(void);
void ramdevice_cleanup(void);
void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors);

typedef struct
{
	unsigned char boot_type; // 0x00 - Inactive; 0x80 - Active (Bootable)
	unsigned char start_head;
	unsigned char start_sec:6;
	unsigned char start_cyl_hi:2;
	unsigned char start_cyl;
	unsigned char part_type;
	unsigned char end_head;
	unsigned char end_sec:6;
	unsigned char end_cyl_hi:2;
	unsigned char end_cyl;
	unsigned int abs_start_sec;
	unsigned int sec_in_part;
} PartEntry;

static struct dof_device
{
	/* Size is the size of the device (in sectors) */
	unsigned int size;
	/* For exclusive access to our request queue */
	spinlock_t lock;
	/* Our request queue */
	struct request_queue *dof_queue;
	/* This is kernel's representation of an individual disk device */
	struct gendisk *dof_disk;
} dof_dev;


typedef PartEntry PartTable[2];

static PartTable def_part_table =
{
	{
		boot_type: 0x00,
		start_head: 0x00,
		start_sec: 0x2,
		start_cyl: 0x00,
		part_type: 0x83,
		end_head: 0x00,
		end_sec: 0x20,
		end_cyl: 0x09,
		abs_start_sec: 0x00000001,
		sec_in_part: 0x0000013F
	},
	
	{
		boot_type: 0x00,
		start_head: 0x00,
		start_sec: 0x1,
		start_cyl: 0x14,
		part_type: 0x83,
		end_head: 0x00,
		end_sec: 0x20,
		end_cyl: 0x1F,
		abs_start_sec: 0x00000280,
		sec_in_part: 0x00000180
	},
	
};



void copy_mbr(u8 *disk)
{
	memset(disk, 0x0, MBR_SIZE);
	*(unsigned long *)(disk + MBR_DISK_SIGNATURE_OFFSET) = 0x36E5756D;
	memcpy(disk + PARTITION_TABLE_OFFSET, &def_part_table, PARTITION_TABLE_SIZE);
	*(unsigned short *)(disk + MBR_SIGNATURE_OFFSET) = MBR_SIGNATURE;
}

int ramdevice_init(void)
{
	dev_data = vmalloc(DOF_DEVICE_SIZE * DOF_SECTOR_SIZE);
	if (dev_data == NULL)
		return -ENOMEM;
	/* Setup its partition table */
	copy_mbr(dev_data);
	return DOF_DEVICE_SIZE;
}
void ramdevice_cleanup(void)
{
	vfree(dev_data);
}

void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(dev_data + sector_off * DOF_SECTOR_SIZE, buffer,
		sectors * DOF_SECTOR_SIZE);
}
void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors)
{
	memcpy(buffer, dev_data + sector_off * DOF_SECTOR_SIZE,
		sectors * DOF_SECTOR_SIZE);
}

static int dof_open(struct block_device *bdev, fmode_t mode)
{
	unsigned unit = iminor(bdev->bd_inode);

	printk(KERN_INFO "dof: Device is opened\n");
	printk(KERN_INFO "dof: Inode number is %d\n", unit);

	if (unit > DOF_MINOR_CNT)
		return -ENODEV;
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
static int dof_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "dof: Device is closed\n");
	return 0;
}
#else
static void dof_close(struct gendisk *disk, fmode_t mode)
{
	printk(KERN_INFO "dof: Device is closed\n");
}
#endif

static int dof_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	geo->heads = 1;
	geo->cylinders = 32;
	geo->sectors = 32;
	geo->start = 0;
	return 0;
}

static int dof_transfer(struct request *req)
{
	//struct dof_device *dev = (struct dof_device *)(req->rq_disk->private_data);

	int dir = rq_data_dir(req);
	sector_t start_sector = blk_rq_pos(req);
	unsigned int sector_cnt = blk_rq_sectors(req);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
#define BV_PAGE(my_bv) ((my_bv)->bv_page)
#define BV_OFFSET(my_bv) ((my_bv)->bv_offset)
#define BV_LEN(my_bv) ((my_bv)->bv_len)
	struct bio_vec *my_bv;
#else
#define BV_PAGE(my_bv) ((my_bv).bv_page)
#define BV_OFFSET(my_bv) ((my_bv).bv_offset)
#define BV_LEN(my_bv) ((my_bv).bv_len)
	struct bio_vec my_bv;
#endif
	struct req_iterator iter;

	sector_t sector_offset;
	unsigned int sectors;
	u8 *buffer;

	int ret = 0;

	//printk(KERN_DEBUG "rb: Dir:%d; Sec:%lld; Cnt:%d\n", dir, start_sector, sector_cnt);

	sector_offset = 0;
	rq_for_each_segment(my_bv, req, iter)
	{
		buffer = page_address(BV_PAGE(my_bv)) + BV_OFFSET(my_bv);
		if (BV_LEN(my_bv) % DOF_SECTOR_SIZE != 0)
		{
			printk(KERN_ERR "dof: Should never happen: "
				"bio size (%d) is not a multiple of DOF_SECTOR_SIZE (%d).\n"
				"This may lead to data truncation.\n",
				BV_LEN(my_bv), DOF_SECTOR_SIZE);
			ret = -EIO;
		}
		sectors = BV_LEN(my_bv) / DOF_SECTOR_SIZE;
		printk(KERN_DEBUG "rb: Start Sector: %llu, Sector Offset: %llu; Buffer: %p; Length: %u sectors\n",
			(unsigned long long)(start_sector), (unsigned long long)(sector_offset), buffer, sectors);
		if (dir == WRITE) /* Write to the device */
		{
			ramdevice_write(start_sector + sector_offset, buffer, sectors);
		}
		else /* Read from the device */
		{
			ramdevice_read(start_sector + sector_offset, buffer, sectors);
		}
		sector_offset += sectors;
	}
	if (sector_offset != sector_cnt)
	{
		printk(KERN_ERR "rb: bio info doesn't match with the request info");
		ret = -EIO;
	}

	return ret;
}

static void dof_request(struct request_queue *q)
{
	struct request *req;
	int ret;

	/* Gets the current request from the dispatch queue */
	while ((req = blk_fetch_request(q)) != NULL)
	{
#if 0
		/*
		 * This function tells us whether we are looking at a filesystem request
		 * - one that moves block of data
		 */
		if (!blk_fs_request(req))
		{
			printk(KERN_NOTICE "rb: Skip non-fs request\n");
			/* We pass 0 to indicate that we successfully completed the request */
			__blk_end_request_all(req, 0);
			//__blk_end_request(req, 0, blk_rq_bytes(req));
			continue;
		}
#endif
		ret = dof_transfer(req);
		__blk_end_request_all(req, ret);
		//__blk_end_request(req, ret, blk_rq_bytes(req));
	}
}

static struct block_device_operations dof_fops =
{
	.owner = THIS_MODULE,
	.open = dof_open,
	.release = dof_close,
	.getgeo = dof_getgeo,
};

static int __init dof_init(void)
{
	int ret;

	/* Set up our RAM Device */
	if ((ret = ramdevice_init()) < 0)
	{
		return ret;
	}
	dof_dev.size = ret;

	/* Get Registered */
	dof_major = register_blkdev(dof_major, "dof");
	if (dof_major <= 0)
	{
		printk(KERN_ERR "rb: Unable to get Major Number\n");
		ramdevice_cleanup();
		return -EBUSY;
	}

	/* Get a request queue (here queue is created) */
	spin_lock_init(&dof_dev.lock);
	dof_dev.dof_queue = blk_init_queue(dof_request, &dof_dev.lock);
	if (dof_dev.dof_queue == NULL)
	{
		printk(KERN_ERR "dof: blk_init_queue failure\n");
		unregister_blkdev(dof_major, "dof");
		ramdevice_cleanup();
		return -ENOMEM;
	}
	
	/*
	 * Add the gendisk structure
	 * By using this memory allocation is involved, 
	 * the minor number we need to pass bcz the device 
	 * will support this much partitions 
	 */
	dof_dev.dof_disk = alloc_disk(DOF_MINOR_CNT);
	if (!dof_dev.dof_disk)
	{
		printk(KERN_ERR "dof: alloc_disk failure\n");
		blk_cleanup_queue(dof_dev.dof_queue);
		unregister_blkdev(dof_major, "dof");
		ramdevice_cleanup();
		return -ENOMEM;
	}

 	/* Setting the major number */
	dof_dev.dof_disk->major = dof_major;
  	/* Setting the first mior number */
	dof_dev.dof_disk->first_minor = DOF_FIRST_MINOR;
 	/* Initializing the device operations */
	dof_dev.dof_disk->fops = &dof_fops;
 	/* Driver-specific own internal data */
	dof_dev.dof_disk->private_data = &dof_dev;
	dof_dev.dof_disk->queue = dof_dev.dof_queue;
	/*
	 * You do not want partition information to show up in 
	 * cat /proc/partitions set this flags
	 */
	//dof_dev.dof_disk->flags = GENHD_FL_SUPPRESS_PARTITION_INFO;
	sprintf(dof_dev.dof_disk->disk_name, "dof");
	/* Setting the capacity of the device in its gendisk structure */
	set_capacity(dof_dev.dof_disk, dof_dev.size);

	/* Adding the disk to the system */
	add_disk(dof_dev.dof_disk);
	/* Now the disk is "live" */
	printk(KERN_INFO "rb: Ram Block driver initialised (%d sectors; %d bytes)\n",
		dof_dev.size, dof_dev.size * DOF_SECTOR_SIZE);

	return 0;
}
static void __exit dof_cleanup(void)
{
	del_gendisk(dof_dev.dof_disk);
	put_disk(dof_dev.dof_disk);
	blk_cleanup_queue(dof_dev.dof_queue);
	unregister_blkdev(dof_major, "dof");
	ramdevice_cleanup();
}

module_init(dof_init);
module_exit(dof_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Swetha Bulusu <h20180124@goa.bits-pilani.ac.in>");
MODULE_DESCRIPTION("DiskRom Driver");
MODULE_ALIAS_BLOCKDEV_MAJOR(dof_major);
