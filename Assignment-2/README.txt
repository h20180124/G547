Disk On RAM Block Driver

Use make command to build the "DISK ON RAM" block driver

Load the driver main.ko using insmod.Commandhas be run giving the root priviledges.
This then creates a block device file appearing to be a RAM of 512Kb.

With the help of "ls -l /dev" command,check the 2 partitions that have been created while loading.
The 2 partitions appear to be "dof1" and "dof2".
The name of the block device file is "dof"

Read the disk using disk dump utility dd

A text can be written into the block device (into a certain partition) using cat command

"cat > /dev/dof1"  (In root)

Going to the root.
Use "sudo su".
Coming out of the root
Use "exit"

To read the file written into the partition use
"cat /dev/dof1"

To display the initial contents of the partition another command "xxd /dev/dof1 | less" can be used.
Display the partition information for the disk using fdisk. 

To unload the module use rmmod command with root permissions.

