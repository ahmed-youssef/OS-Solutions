/*
 * COMP 310/ECSE 427
 * Winter 2015
 * Programming Assignment 1 Solution
 * Author: Ahmed Youssef
 * sfs_api.c (Simple Filesystem Implementation using I-nodes)
 * 
 * Use the provided make file to compile.
 * ./sfs -s mnt/ to mount the filesystem on mnt/ directory using FUSE
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sfs_api.h"
#include "disk_emu.h"

#define FREE 1
#define USED 0

#define NUMDIRECTPOINTERS 12
#define MAXINODENUM 100
#define BLKSIZ 512
#define DISKIMG "sfs_disk.img"
#define MAXOPENFILES 32
#define ROOTINODENUM 0
#define MAGICNUM 0xAABB0005
#define INODETBLSIZ 100
#define SUPERBLOCKADD 0
#define DBBITMAPADD 1
#define INBITMAPADD 2
#define INSTARTADD 3
#define DBSTARTADD 18
#define ROOTFD 0
#define BLKPOINTERSIZ 4
#define BLOCKNUM DBSTARTADD + BLKSIZ
#define MAXFILESIZ BLKSIZ*(NUMDIRECTPOINTERS + BLKSIZ/BLKPOINTERSIZ)
#define INPERBLK 7

#define max(A,B)                    ( (A) > (B) ? (A):(B))
#define min(A,B)                    ( (A) < (B) ? (A):(B))

typedef struct super_block_t {
    int magic_number;
    int block_size;
    int fs_size;
    int inode_tbl_length;
    int root_inode;
} super_block;

typedef struct blk_pointer_t {
    int direct[NUMDIRECTPOINTERS];
    int indirect;
} blk_pointers_t;

typedef struct inode_struct_t {
    int mode;
    int link_cnt;
    int uid;
    int gid;
    int size;
    blk_pointers_t blk_pointers;
} inode_struct;

typedef struct fd_entry_t {
    char status;
    int inode_num;
    inode_struct* inode;
    int rw_pointer;
} fd_entry;

typedef struct directory_entry_t {
    int inode_num;
    char file_name[MAXFILENAME];
} directory_map;

fd_entry fd_table[MAXOPENFILES];

int get_inode_num_by_name(const char* name);
int get_free_inode_num();
int get_free_fd();
int get_free_datablock();
inode_struct* get_inode_by_index(int inode_num);
int get_blk_pointer(blk_pointers_t* pointers, int blknum);
void free_blk_pointers(blk_pointers_t* pointers);
void write_inode_by_index(int inode_num, inode_struct* inode);
void remove_inode_from_directory(char* name);

int mksfs(int fresh)
{
    super_block spr_block;
    char buff[BLKSIZ];
    int i;
    
    // Initialize fd_table
    for(i = 0; i < MAXOPENFILES; i++)
        fd_table[i].status = FREE;
            
    if(fresh)
    {
        // Initialize and write super block to disk
        init_fresh_disk(DISKIMG, BLKSIZ, BLOCKNUM);
        spr_block.magic_number = MAGICNUM;
        spr_block.block_size = BLKSIZ;
        spr_block.fs_size = BLOCKNUM;
        spr_block.inode_tbl_length = INODETBLSIZ;
        spr_block.root_inode = ROOTINODENUM;
        write_blocks(SUPERBLOCKADD, 1,  (void*) &spr_block);
        
        // Setup datablock bitmap
        memset(buff, FREE, BLKSIZ);
        write_blocks(DBBITMAPADD, 1, buff);
        
        // Setup INODE Bitmap and allocate first i-node entry to root directory
        buff[ROOTINODENUM] = USED;
        write_blocks(INBITMAPADD, 1,  buff);
        
        // Allocate fd to root directory
        fd_table[ROOTFD].status = USED;
        fd_table[ROOTFD].inode_num = ROOTINODENUM;
        fd_table[ROOTFD].rw_pointer = 0;
        fd_table[ROOTFD].inode = (inode_struct*) calloc(1, sizeof(inode_struct));
        fd_table[ROOTFD].inode->size = 0;

        write_inode_by_index(fd_table[ROOTFD].inode_num, fd_table[ROOTFD].inode);
    } else 
    {
        init_disk(DISKIMG, BLKSIZ, BLOCKNUM);
        fd_table[ROOTFD].status = USED;
        fd_table[ROOTFD].inode_num = ROOTINODENUM;
        fd_table[ROOTFD].inode = get_inode_by_index(ROOTINODENUM);
        fd_table[ROOTFD].rw_pointer = fd_table[ROOTFD].inode->size;
    }   
    
    return 0;
}

int sfs_fopen(char *name) 
{
    int inode_num, fd, i; 
    inode_struct* inode;
    directory_map new_file;
    
    inode_num = get_inode_num_by_name(name);
    
    // Check if file is already open
    for(i = 1; i < MAXOPENFILES; i++) {
        if( (fd_table[i].status == USED) && (fd_table[i].inode_num == inode_num)) return i;
    }
    
    fd = get_free_fd();
    if(fd == -1) return -1;
        
    if(inode_num == -1) // File does not exist, create file 
    {
        // Allocate i-node
        inode_num = get_free_inode_num();
        inode = calloc(1, sizeof(inode_struct));
        
        // Add new file to directory
        new_file.inode_num = inode_num;
        strcpy(new_file.file_name, name);
        
        // Seek to end of directory and write new file entry
        fd_table[ROOTFD].rw_pointer = fd_table[ROOTFD].inode->size;
        sfs_fwrite(ROOTFD, (char*)&new_file, sizeof(directory_map));
        write_inode_by_index(fd_table[ROOTFD].inode_num, fd_table[ROOTFD].inode);
    } else {
        inode = get_inode_by_index(inode_num);
    } 
    
    fd_table[fd].inode_num = inode_num;
    fd_table[fd].inode = inode;
    fd_table[fd].rw_pointer = inode->size;
    
    return fd;
}

int sfs_fwrite(int fileID, const char *buf, int length)
{
    inode_struct* inode;
    int filled, rw_pointer, blknum, blk_pointer;
    int last_block_bytes, last_full_blknum; 
    char block[BLKSIZ];
    
    // If file is not open or write exceeds maximum file size return error
    if( (fd_table[fileID].status == FREE) || (fd_table[fileID].rw_pointer + length > MAXFILESIZ) )
            return -1;
    
    inode = fd_table[fileID].inode;
    rw_pointer = fd_table[fileID].rw_pointer;
    
    blknum = rw_pointer/BLKSIZ;
    filled = rw_pointer % BLKSIZ;
    
    // If we do not need to go to another data block
    if( (filled + length) <= BLKSIZ) {
        blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
        if(blk_pointer == -1) return -1;
        read_blocks(blk_pointer, 1, block);
        memcpy(&block[filled], buf, length);
        write_blocks(blk_pointer, 1, block);
        
        inode->size = max(rw_pointer + length, inode->size);
        fd_table[fileID].rw_pointer = rw_pointer + length;
        return length;
    } 
    
    last_full_blknum = ((filled + length) / BLKSIZ) + blknum;
    last_block_bytes = (filled + length) % BLKSIZ;
    
    blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
    if(blk_pointer == -1) return -1;
    read_blocks(blk_pointer, 1, block);
    memcpy(&block[filled], buf, BLKSIZ - filled);
    write_blocks(blk_pointer, 1, block);
    
    buf += BLKSIZ - filled;
    blknum++;
    
    while(blknum < last_full_blknum) {
        blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
        if(blk_pointer == -1) return -1;
        memcpy(block, buf, BLKSIZ);
        write_blocks(blk_pointer, 1, block);
        
        buf += BLKSIZ;
        blknum++;
    }
    
    blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
    if(blk_pointer == -1) return -1;
    read_blocks(blk_pointer, 1, block);
    memcpy(block, buf, last_block_bytes);
    write_blocks(blk_pointer, 1, block);
    
    inode->size = max(rw_pointer + length, inode->size);
    fd_table[fileID].rw_pointer = rw_pointer + length;
    return length;
}

int sfs_fclose(int fileID)
{
    if(fd_table[fileID].status == FREE) return -1;
    
    // Write inode to disk and free it from memory
    write_inode_by_index(fd_table[fileID].inode_num, fd_table[fileID].inode);
    free(fd_table[fileID].inode);
    
    fd_table[fileID].status = FREE;
    return 0;
}

int sfs_fseek(int fileID, int offset)
{
    if(fd_table[fileID].status == FREE) return -1;
    
    if(offset > fd_table[fileID].inode->size) 
        offset = fd_table[fileID].inode->size;
    
    fd_table[fileID].rw_pointer = offset;
    
    return 0;
}

int sfs_fread(int fileID, char *buf, int length)
{
    inode_struct* inode;
    int rw_pointer, blknum, filled;
    int blk_pointer, last_full_blknum, last_block_bytes;
    char block[BLKSIZ];
    
    if(fd_table[fileID].status == FREE) return -1;
        
    inode = fd_table[fileID].inode;
    
    if(fd_table[fileID].rw_pointer + length > inode->size)
        length = inode->size - fd_table[fileID].rw_pointer;
    
    inode = fd_table[fileID].inode;
    rw_pointer = fd_table[fileID].rw_pointer;
    
    blknum = rw_pointer/BLKSIZ;
    filled = rw_pointer % BLKSIZ;

    // If we do not need to go to another data block
    if( (filled + length) <= BLKSIZ) {
        blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
        read_blocks(blk_pointer, 1, block);
        memcpy(buf, &block[filled], length);
        fd_table[fileID].rw_pointer += length;
        return length;
    } 
    
    last_full_blknum = ((filled + length) / BLKSIZ) + blknum;
    last_block_bytes = (filled + length) % BLKSIZ;
    
    
    blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
    read_blocks(blk_pointer, 1, block);
    memcpy(buf, &block[filled], BLKSIZ - filled);
    buf += BLKSIZ - filled;
    blknum++;
    
    while(blknum < last_full_blknum) {
        blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
        read_blocks(blk_pointer, 1, block);
        memcpy(buf, block, BLKSIZ);
        buf += BLKSIZ;
        blknum++;
    }
    
    blk_pointer = get_blk_pointer(&(inode->blk_pointers), blknum);
    read_blocks(blk_pointer, 1, block);
    memcpy(buf, block, last_block_bytes);
    
    fd_table[fileID].rw_pointer += length;
    return length;    
}

int sfs_remove(char *file)
{
    int inode_num;
    inode_struct* inode;
    char bitmap[BLKSIZ];
    
    inode_num = get_inode_num_by_name(file);
    if(inode_num == -1) return -1;
    
    inode = get_inode_by_index(inode_num);
    
    // Free Data blocks
    free_blk_pointers(&(inode->blk_pointers));
    
    // Free Inode
    read_blocks(INBITMAPADD, 1, bitmap);
    bitmap[inode_num] = FREE;
    write_blocks(INBITMAPADD, 1, bitmap);
    
    remove_inode_from_directory(file);
    
    free(inode);
    return 0;
}

int sfs_GetFileSize(const char* path)
{
    int inode_num, size;
    inode_struct* inode;
    
    inode_num = get_inode_num_by_name(path);
    if(inode_num == -1) return -1;
    
    inode = get_inode_by_index(inode_num);
    size = inode->size;
    free(inode);
    return size;
}

int sfs_get_next_filename(char* filename)
{
    static int i=0;
    //static char* root_dir;
    static int num_files;
    static directory_map* dir_map;
    
    if(i == 0) {
        dir_map = malloc(fd_table[ROOTFD].inode->size);
        fd_table[ROOTFD].rw_pointer = 0;
        sfs_fread(ROOTFD, (char*)dir_map, fd_table[ROOTFD].inode->size);
        num_files = fd_table[ROOTFD].inode->size/sizeof(directory_map);
    }
    
    while(i < num_files) {
        strcpy(filename, dir_map[i].file_name);
        i++;
        return 1;
    }
    
    free(dir_map);
    i = 0;
    return 0;
}

int get_free_fd()
{
    int i;
    
    for(i = 1; i < MAXOPENFILES; i++) {
        if(fd_table[i].status == FREE) {
            fd_table[i].status = USED;
            return i;
        }
    } 
    
    return -1;
}

int get_free_datablock()
{
    char db_bitmap[BLKSIZ];
    int i;
    
    read_blocks(DBBITMAPADD, 1, db_bitmap);
    for(i = 0; i < BLKSIZ; i++) {
        if(db_bitmap[i] == FREE) {
            db_bitmap[i] = USED;
            write_blocks(DBBITMAPADD, 1, db_bitmap);
            return DBSTARTADD+i;
        }
    }  
    printf("Ran out of free data blocks\n");
    return 0;
}

int get_inode_num_by_name(const char* name)
{
    int i, num_entries, inode_num;
    int dir_size = fd_table[ROOTFD].inode->size;
    char* buf = (char*)malloc(dir_size);
    directory_map* dir_entry = (directory_map*)buf;
    
    fd_table[ROOTFD].rw_pointer = 0;
    sfs_fread(ROOTFD, buf, dir_size);
    
    num_entries = dir_size/sizeof(directory_map);
    
    for(i = 0; i < num_entries; i++) {
        if(strcmp(dir_entry->file_name, name) == 0) {
            inode_num = dir_entry->inode_num;
            free(buf);
            return inode_num;
        }
        dir_entry++;
    }
    
    free(buf);
    return -1;
}

void remove_inode_from_directory(char* name)
{
    int i, num_entries;
    int dir_size = fd_table[ROOTFD].inode->size;
    char* buf = (char*)malloc(dir_size);
    directory_map* dir_entry = (directory_map*)buf;
    
    fd_table[ROOTFD].rw_pointer = 0;
    sfs_fread(ROOTFD, buf, dir_size);
    
    num_entries = dir_size/sizeof(directory_map);
    
    for(i = 0; i < num_entries; i++) {
        if(strcmp(dir_entry->file_name, name) == 0) {
            memmove(dir_entry, &dir_entry[1], sizeof(directory_map)*(num_entries-i-1));
            
            // Truncate root directory inode
            fd_table[ROOTFD].rw_pointer = 0;
            fd_table[ROOTFD].inode->size = 0;
            free_blk_pointers(&(fd_table[ROOTFD].inode->blk_pointers));
            memset(fd_table[ROOTFD].inode, 0, sizeof(inode_struct));
            sfs_fwrite(ROOTFD, buf, dir_size-sizeof(directory_map));
            write_inode_by_index(fd_table[ROOTFD].inode_num, fd_table[ROOTFD].inode);
            free(buf);
            return;
        }
        dir_entry++;
    }
}

int get_blk_pointer(blk_pointers_t* pointers, int blknum)
{
    int indirect_pointers[BLKSIZ/BLKPOINTERSIZ];
    
    // If blk num is for a direct pointer
    if(blknum < NUMDIRECTPOINTERS) {
        if(pointers->direct[blknum] == 0) {
            if( (pointers->direct[blknum] = get_free_datablock()) == 0) return -1;     
        }
        return pointers->direct[blknum];
    }
    
    // If block num is the first indirect pointer entry
    if(pointers->indirect == 0) {
        if( (pointers->indirect = get_free_datablock()) == 0) return -1;
        memset(indirect_pointers, 0, BLKSIZ);
        write_blocks(pointers->indirect, 1, (void*)indirect_pointers);
    }
    
    // If block num is an indirect pointer entry that is not the first
    read_blocks(pointers->indirect, 1, (void*)indirect_pointers);  
    if(indirect_pointers[blknum - NUMDIRECTPOINTERS] == 0) {
        if( (indirect_pointers[blknum - NUMDIRECTPOINTERS] = get_free_datablock()) == 0) return -1;
        write_blocks(pointers->indirect, 1, (void*)indirect_pointers);
    }
    return indirect_pointers[blknum - NUMDIRECTPOINTERS];
}

void free_blk_pointers(blk_pointers_t* pointers)
{
    int indirect_pointers[BLKSIZ/BLKPOINTERSIZ];
    char bitmap[BLKSIZ];
    int db_index, blk_num=0;
    
    read_blocks(DBBITMAPADD, 1, bitmap);
    
    for(blk_num = 0; blk_num <= NUMDIRECTPOINTERS; blk_num++) {
        db_index = pointers->direct[blk_num];
        if(db_index == 0) {
            write_blocks(DBBITMAPADD, 1, bitmap);
            return;
        }
        db_index -= DBSTARTADD;
        bitmap[db_index] = FREE;    
    }
    
    read_blocks(pointers->indirect, 1, (void*)indirect_pointers);  
    for(blk_num = 0; blk_num < BLKSIZ/BLKPOINTERSIZ; blk_num++) {
        db_index = indirect_pointers[blk_num];
        if(db_index == 0) {
            write_blocks(DBBITMAPADD, 1, bitmap);
            return;
        }
        db_index -= DBSTARTADD;
        bitmap[db_index] = FREE;
    }
}

int get_free_inode_num()
{
    char in_bitmap[BLKSIZ];
    int i;
    
    read_blocks(INBITMAPADD, 1, in_bitmap);
    for(i = 0; i < MAXINODENUM; i++) {
        if(in_bitmap[i] == FREE) {
            in_bitmap[i] = USED;
            write_blocks(INBITMAPADD, 1, in_bitmap);
            return i;
        }
    }
    return -1;   
}

inode_struct* get_inode_by_index(int inode_num)
{
    inode_struct* inode, *inode_array;
    int blk_num, index;
    char buf[BLKSIZ];
    
    inode = malloc(sizeof(inode_struct));
    
    blk_num = inode_num/INPERBLK;
    index = inode_num % INPERBLK;
    read_blocks(INSTARTADD+blk_num, 1, buf);
    inode_array = (inode_struct*)buf;
    memcpy(inode, &inode_array[index], sizeof(inode_struct));
    
    return inode;
}

void write_inode_by_index(int inode_num, inode_struct* inode)
{
    inode_struct *inode_array;
    int blk_num, index;
    char buf[BLKSIZ];
    
    blk_num = inode_num/INPERBLK;
    index = inode_num % INPERBLK;
    read_blocks(INSTARTADD+blk_num, 1, buf);
    inode_array = (inode_struct*)buf;
    memcpy(&inode_array[index], inode, sizeof(inode_struct));
    write_blocks(INSTARTADD+blk_num, 1, buf);  
}
