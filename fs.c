#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   64
#define POINTERS_PER_INODE 14

struct fs_superblock {
	unsigned int magic;
	unsigned int nblocks;
	unsigned int ninodeblocks;
	unsigned int ninodes;
};
struct fs_superblock my_super;

struct fs_inode {
	unsigned int isvalid;
	unsigned int size;
	unsigned int direct[POINTERS_PER_INODE];
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

#define FALSE 0
#define TRUE 1

#define VALID 1
#define NON_VALID 0

#define FREE 0
#define NOT_FREE 1
unsigned char * blockBitMap;

struct fs_inode inode;

void print_bitmap();

int fs_format()
{
	union fs_block block;
	unsigned int i, nblocks;
	int ninodeblocks;

	if(my_super.magic == FS_MAGIC){
		printf("Cannot format a mounted disk!\n");
		return -1;
	}

	nblocks = disk_size();
	block.super.magic = FS_MAGIC;
	block.super.nblocks = nblocks;
	ninodeblocks = (int)ceil((float)nblocks*0.1);
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = block.super.ninodeblocks * INODES_PER_BLOCK;

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	/* escrita do superbloco */
	disk_write(0,block.data);

	/* prepara��o da tabela de inodes */
	bzero(block.data, DISK_BLOCK_SIZE);
	for( i = 0; i < INODES_PER_BLOCK; i++ )
		block.inode[i].isvalid = NON_VALID;

	/* escrita da tabela de inodes */
	for( i = 1; i <= ninodeblocks; i++)
		disk_write( i, block.data );

	return 0;
}

void fs_debug()
{
	union fs_block sBlock;
	union fs_block iBlock;
	unsigned int i, j, k;

	disk_read(0,sBlock.data);

	if(sBlock.super.magic != FS_MAGIC){
		printf("disk unformatted !\n");
		return;
	}
	printf("superblock:\n");
	printf("    %d blocks\n",sBlock.super.nblocks);
	printf("    %d inode blocks\n",sBlock.super.ninodeblocks);
	printf("    %d inodes\n",sBlock.super.ninodes);


	for( i = 1; i <= sBlock.super.ninodeblocks; i++){
		disk_read( i, iBlock.data );
		for( j = 0; j < INODES_PER_BLOCK; j++)
			if( iBlock.inode[j].isvalid == VALID){
				printf("-----\ninode: %d\n", (i-1)*INODES_PER_BLOCK + j);
				printf("size: %d \n",iBlock.inode[j].size);
				printf("blocks:");
				for( k = 0; k < POINTERS_PER_INODE; k++)
					if (iBlock.inode[j].direct[k]!=0)
						printf("  %d",iBlock.inode[j].direct[k]);
				printf("\n");
			}
	}
}

int fs_mount()
{
	union fs_block block;
	int i, j, k;

	if(my_super.magic == FS_MAGIC){
		printf("disc already mounted!\n");
		return -1;
	}

	disk_read(0,block.data);
	if(block.super.magic != FS_MAGIC){
		printf("cannot mount an unformatted disc!\n");
		return -1;
	}
	if(block.super.nblocks != disk_size()){
		printf("file system size and disk size differ!\n");
		return -1;
	}

	disk_read(0, block.data);

	int bitMapAlloc = disk_size();

	my_super.nblocks = block.super.nblocks;
	my_super.magic = FS_MAGIC;
	my_super.ninodeblocks = block.super.ninodeblocks;
	my_super.ninodes = block.super.ninodes;

	blockBitMap = (unsigned char *) malloc(bitMapAlloc);

	unsigned int ninodeblocks_tmp = block.super.ninodeblocks;

	for(k = 0; k <= ninodeblocks_tmp; k++) blockBitMap[k] = NOT_FREE;

	for(k = ninodeblocks_tmp + 1; k < bitMapAlloc;k++) {
		for( i = 1; i <= ninodeblocks_tmp; i++){
			disk_read( i, block.data );
			for( j = 0; j < INODES_PER_BLOCK; j++)
				if( block.inode[j].isvalid == VALID) {
					blockBitMap[k] = NOT_FREE;
                    for(int d=0;d<POINTERS_PER_INODE;d++) {
                        if(block.inode[j].direct[d] != 0) {
                            blockBitMap[block.inode[j].direct[d]] = NOT_FREE;
                        }
                    }
				} else {
					blockBitMap[k] = FREE;
				}
		}
	}

	return 0;
}


int fs_create()
{
	int freeInode, inodeBlock;
	union fs_block block;

	if(my_super.magic != FS_MAGIC){
		printf("disc not mounted\n");
		return -1;
	}

	int found = 0;
	freeInode = -1;
	for(int i=1;i<=my_super.ninodeblocks && !(found);i++) {
		disk_read(i,block.data);
		for(int j=0;j<INODES_PER_BLOCK;j++) {
			if(block.inode[j].isvalid == NON_VALID) {
				found = 1;
				block.inode[j].isvalid = VALID;
				block.inode[j].size = 0;

                for(int k=0;k<POINTERS_PER_INODE;k++) {
                    block.inode[j].direct[k] = 0;
                }

				freeInode = (i-1)*INODES_PER_BLOCK + j;
				inodeBlock = 1 + (freeInode/INODES_PER_BLOCK);
                blockBitMap[i] = NOT_FREE;
				break;
			}
		}
        disk_write(inodeBlock, block.data);
	}
	return freeInode;
}

void inode_load( int inumber, struct fs_inode *inode ){
	int inodeBlock;
	union fs_block block;

	if( inumber > my_super.ninodes ){
		printf("inode number too big \n");
		abort();
	}
	inodeBlock = 1 + (inumber/INODES_PER_BLOCK);
	disk_read( inodeBlock, block.data );
	*inode = block.inode[inumber % INODES_PER_BLOCK];
}

void inode_save( int inumber, struct fs_inode *inode ){
	int inodeBlock;
	union fs_block block;

	if( inumber > my_super.ninodes ){
		printf("inode number too big \n");
		abort();
	}
	inodeBlock = 1 + (inumber/INODES_PER_BLOCK);
	disk_read(inodeBlock, block.data);
	block.inode[inumber % INODES_PER_BLOCK] = *inode;
	disk_write( inodeBlock, block.data );
}

int fs_close(int inumber) {

    union fs_block block_f;

    inode_load(inumber,&inode);
	if(inode.isvalid == NON_VALID) {
		return -1;
	}

    for(int i=0;i<POINTERS_PER_INODE;i++) {
        if(inode.direct[i] != 0) {
            int block = inode.direct[i];
            disk_read_data(block,block_f.data);
            disk_write(block,block_f.data);
        }
    }

    return 0;

}

int fs_delete( int inumber )
{
	int i;

	if(my_super.magic != FS_MAGIC){
		printf("disc not mounted\n");
		return -1;
	}
     if(inumber > my_super.ninodes || inumber < 0) {
        return -1;
     }

	inode_load(inumber,&inode);
	if(inode.isvalid == NON_VALID) {
		return -1;
	}

	inode.isvalid = NON_VALID;
	inode_save(inumber,&inode);

	for(i=0;i<POINTERS_PER_INODE;i++){
		if(inode.direct[i] != 0)
			blockBitMap[inode.direct[i]] = FREE;
	}

	return 0;
}

int fs_getsize( int inumber )
{

	if(my_super.magic != FS_MAGIC){
		printf("disc not mounted\n");
		return -1;
	}

	inode_load(inumber,&inode);

	if(inode.isvalid == NON_VALID) {
		printf("Inode number is invalid!\n");
	}

	return inode.size;

}


/**************************************************************/
int fs_read( int inumber, char *data, int length, int offset )
{
	int bytesToRead = 0;
	union fs_block buff;

	if(my_super.magic != FS_MAGIC){
		printf("disc not mounted\n");
		return -1;
	}
	inode_load( inumber, &inode );
	if( inode.isvalid == NON_VALID ){
		printf("inode is not valid\n");
		return -1;
	}

	if( offset > inode.size ){
		printf("offset bigger than file size !\n");
		return -1;
	}

	int offset_in_first_block = offset % DISK_BLOCK_SIZE;
	int first_block = offset / DISK_BLOCK_SIZE;
	int bytes_left = (length < inode.size-offset ? length:inode.size-offset);

	for(int j=first_block;inode.direct[j] != 0 && bytes_left>0;j++) {

		int data_block = inode.direct[j];
        disk_read_data(data_block,buff.data);
		//disk_read(data_block, buff.data);

		for(int i=(bytesToRead == 0 ? offset_in_first_block:0);i<DISK_BLOCK_SIZE && bytes_left>0;i++) {
			data[bytesToRead++] = buff.data[i];
			bytes_left--;
		}
	}
	return bytesToRead;
}

/******************************************************************/
int getFreeBlock(){
	int i, found;

	i = 0;
	found = FALSE;
	do{
		if(blockBitMap[i] == FREE){
			found = TRUE;
			blockBitMap[i] = NOT_FREE;
		}
		else i++;
	}while((!found) && (i < my_super.nblocks));

	if(i == my_super.nblocks) return -1; /* nao ha' blocos livres */
	else return i;
}


int fs_write( int inumber, char *data, int length, int offset )
{

    int bytesToWrite=0;
	union fs_block buff;

	if(my_super.magic != FS_MAGIC){
		printf("disc not mounted\n");
		return -1;
	}
	inode_load( inumber, &inode );
	if( inode.isvalid == NON_VALID ){
		printf("inode is not valid\n");
		return -1;
	}
	if( offset > inode.size ){
		printf("starting to write after end of file\n");
		return -1;
	}

    int createdBlock = FALSE;
    int offset_in_first_block = offset % DISK_BLOCK_SIZE;
	int first_block = offset / DISK_BLOCK_SIZE;
	int bytes_left = length;

	for(int j=first_block;bytes_left>0;j++) {

        if(j == POINTERS_PER_INODE) return 0;
        createdBlock = FALSE;
		int data_block = inode.direct[j];

		if(!data_block) {
			data_block = getFreeBlock();
			if(data_block == -1) return -1;
            createdBlock = TRUE;
			inode.direct[j] = data_block;
		}

		disk_read_data(data_block, buff.data);
		for(int i=offset_in_first_block;i<DISK_BLOCK_SIZE && bytes_left>0;i++) {
			buff.data[i] = data[bytesToWrite++];
			bytes_left--;
		}
        disk_write_data(data_block,buff.data);
	}

    if(createdBlock == TRUE) {
        inode.size += bytesToWrite;
    }
    else {
        inode.size += bytesToWrite - (((offset%DISK_BLOCK_SIZE) > 0) ? (bytesToWrite - (offset%DISK_BLOCK_SIZE)) : 0);
    }

	inode_save( inumber, &inode);

	return bytesToWrite;
}
