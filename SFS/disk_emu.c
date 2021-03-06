Assign_3/fuse_wrappers.c                                                                            0000664 0001750 0001750 00000007167 12507123621 014243  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   #define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include "disk_emu.h"
#include "sfs_api.h"


static int fuse_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    int size;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if((size = sfs_GetFileSize(&path[1])) != -1) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = size;
    } else
        res = -ENOENT;
    
    return res;
}

static int fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
        off_t offset, struct fuse_file_info *fi)
{
    char file_name[MAXFILENAME];
    
    if (strcmp(path, "/") != 0)
        return -ENOENT;
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    
    while(sfs_get_next_filename(file_name)) {
        filler(buf, file_name, NULL, 0);
    }
    
    return 0;
}

static int fuse_unlink(const char *path)
{
    int res;
    char filename[MAXFILENAME];
    
    strcpy(filename, &path[1]);
    res = sfs_remove(filename);
    if (res == -1)
        return -errno;
    
    return 0;
}

static int fuse_open(const char *path, struct fuse_file_info *fi)
{
    int res;
    char filename[MAXFILENAME];
    
    strcpy(filename, &path[1]);
    res = sfs_fopen(filename);
    if (res == -1)
        return -errno;
    
    sfs_fclose(res);
    return 0;
}

static int fuse_read(const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi)
{
    int fd;
    int res;
    
    char filename[MAXFILENAME];
    
    strcpy(filename, &path[1]);  
    fd = sfs_fopen(filename);
    if (fd == -1)
        return -errno;
    
    if(sfs_fseek(fd, offset) == -1)
        return -errno;
    
    res = sfs_fread(fd, buf, size);
    if (res == -1)
        return -errno;
    
    sfs_fclose(fd);
    return res;
}

static int fuse_write(const char *path, const char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;
    
    char filename[MAXFILENAME];
    
    strcpy(filename, &path[1]);
    
    fd = sfs_fopen(filename);
    if (fd == -1) 
        return -errno;
    
    if(sfs_fseek(fd, offset) == -1)
        return -errno;
    
    res = sfs_fwrite(fd, buf, size);
    if (res == -1)
        return -errno;
    
    sfs_fclose(fd);
    return res;
}

static int fuse_truncate(const char *path, off_t size)
{
    char filename[MAXFILENAME];
    int fd;
    
    strcpy(filename, &path[1]);

    fd = sfs_remove(filename);
    if (fd == -1)
        return -errno;
    
    fd = sfs_fopen(filename);
    sfs_fclose(fd);
    return 0;
}

static int fuse_access(const char *path, int mask)
{
    return 0;
}

static int fuse_mknod(const char *path, mode_t mode, dev_t rdev)
{
    return 0;
}

static int fuse_create (const char *path, mode_t mode, struct fuse_file_info *fp)
{
    char filename[MAXFILENAME];
    int fd;
    
    strcpy(filename, &path[1]);

    fd = sfs_fopen(filename);
    
    sfs_fclose(fd);
    return 0;
}

static struct fuse_operations xmp_oper = {
    .getattr = fuse_getattr,
    .readdir = fuse_readdir,
    .mknod = fuse_mknod,
    .unlink = fuse_unlink,
    .truncate = fuse_truncate,
    .open = fuse_open, 
    .read = fuse_read, 
    .write = fuse_write, 
    .access = fuse_access,
    .create = fuse_create,
};

int main(int argc, char *argv[])
{
    mksfs(1);
    
    return fuse_main(argc, argv, &xmp_oper, NULL);
}
                                                                                                                                                                                                                                                                                                                                                                                                         Assign_3/sfs_api.c                                                                                  0000664 0001750 0001750 00000037331 12511272722 013000  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   /*
 * sfs_api.c (Simple Filesystem Implementation using I-nodes)
 * AUTHOR: Ahmed Youssef
 * DATE: April 6, 2015
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
                                                                                                                                                                                                                                                                                                       Assign_3/sfs_test.c                                                                                 0000664 0001750 0001750 00000026462 12507123526 013213  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   /* sfs_test.c 
 * 
 * Written by Robert Vincent for Programming Assignment #1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sfs_api.h"

/* The maximum file name length. We assume that filenames can contain
 * upper-case letters and periods ('.') characters. Feel free to
 * change this if your implementation differs.
 */
#define MAX_FNAME_LENGTH 20   /* Assume at most 20 characters (16.3) */

/* The maximum number of files to attempt to open or create.  NOTE: we
 * do not _require_ that you support this many files. This is just to
 * test the behavior of your code.
 */
#define MAX_FD 100 

/* The maximum number of bytes we'll try to write to a file. If you
 * support much shorter or larger files for some reason, feel free to
 * reduce this value.
 */
#define MAX_BYTES 30000 /* Maximum file size I'll try to create */
#define MIN_BYTES 10000         /* Minimum file size */

/* Just a random test string.
 */
static char test_str[] = "The quick brown fox jumps over the lazy dog.\n";

/* rand_name() - return a randomly-generated, but legal, file name.
 *
 * This function creates a filename of the form xxxxxxxx.xxx, where
 * each 'x' is a random upper-case letter (A-Z). Feel free to modify
 * this function if your implementation requires shorter filenames, or
 * supports longer or different file name conventions.
 * 
 * The return value is a pointer to the new string, which may be
 * released by a call to free() when you are done using the string.
 */
 
char *rand_name() 
{
  char fname[MAX_FNAME_LENGTH];
  int i;

  for (i = 0; i < MAX_FNAME_LENGTH; i++) {
    if (i != 16) {
      fname[i] = 'A' + (rand() % 26);
    }
    else {
      fname[i] = '.';
    }
  }
  fname[i] = '\0';
  return (strdup(fname));
}

/* The main testing program */

int main(int argc, char **argv)
{
  int i, j, k;
  int chunksize;
  int readsize;
  char *buffer;
  char fixedbuf[1024];
  int fds[MAX_FD];
  char *names[MAX_FD];
  int filesize[MAX_FD];
  int nopen;                    /* Number of files simultaneously open */
  int ncreate;                  /* Number of files created in directory */
  int error_count = 0;
  int tmp;
  char file_name[MAX_FNAME_LENGTH]; 
  
  mksfs(1);                     /* Initialize the file system. */

  /* First we open two files and attempt to write data to them.
   */
  for (i = 0; i < 2; i++) {
    names[i] = rand_name();
    fds[i] = sfs_fopen(names[i]);
    if (fds[i] < 0) {
      fprintf(stderr, "ERROR: creating first test file %s\n", names[i]);
      error_count++;
    }
    tmp = sfs_fopen(names[i]);
    if (tmp >= 0 && tmp != fds[i]) {
      fprintf(stderr, "ERROR: file %s was opened twice\n", names[i]);
      error_count++;
    }
    filesize[i] = (rand() % (MAX_BYTES-MIN_BYTES)) + MIN_BYTES;
  }

  for (i = 0; i < 2; i++) {
    for (j = i + 1; j < 2; j++) {
      if (fds[i] == fds[j]) {
        fprintf(stderr, "Warning: the file descriptors probably shouldn't be the same?\n");
      }
    }
  }

  printf("Two files created with zero length:\n");

  for (i = 0; i < 2; i++) {
    for (j = 0; j < filesize[i]; j += chunksize) {
      if ((filesize[i] - j) < 10) {
        chunksize = filesize[i] - j;
      }
      else {
        chunksize = (rand() % (filesize[i] - j)) + 1;
      }

      if ((buffer = malloc(chunksize)) == NULL) {
        fprintf(stderr, "ABORT: Out of memory!\n");
        exit(-1);
      }
      for (k = 0; k < chunksize; k++) {
        buffer[k] = (char) (j+k);
      }
      tmp = sfs_fwrite(fds[i], buffer, chunksize);
      if (tmp != chunksize) {
        fprintf(stderr, "ERROR: Tried to write %d bytes, but wrote %d\n", 
                chunksize, tmp);
        error_count++;
      }
      free(buffer);
    }
  }

  if (sfs_fclose(fds[1]) != 0) {
    fprintf(stderr, "ERROR: close of handle %d failed\n", fds[1]);
    error_count++;
  }
//
//  /* Sneaky attempt to close already closed file handle. */
  if (sfs_fclose(fds[1]) == 0) {
    fprintf(stderr, "ERROR: close of stale handle %d succeeded\n", fds[1]);
    error_count++;
  }

  printf("File %s now has length %d and %s now has length %d:\n",
         names[0], filesize[0], names[1], filesize[1]);

  /* Just to be cruel - attempt to read from a closed file handle. 
   */
  if (sfs_fread(fds[1], fixedbuf, sizeof(fixedbuf)) > 0) {
    fprintf(stderr, "ERROR: read from a closed file handle?\n");
    error_count++;
  }
  
  //fds[0] = sfs_fopen(names[0]);
  fds[1] = sfs_fopen(names[1]);
  
  sfs_fseek(fds[0], 0);
  sfs_fseek(fds[1], 0);
  
  for (i = 0; i < 2; i++) {
    for (j = 0; j < filesize[i]; j += chunksize) {
      if ((filesize[i] - j) < 10) {
        chunksize = filesize[i] - j;
      }
      else {
        chunksize = (rand() % (filesize[i] - j)) + 1;
      }
      if ((buffer = malloc(chunksize)) == NULL) {
        fprintf(stderr, "ABORT: Out of memory!\n");
        exit(-1);
      }
      readsize = sfs_fread(fds[i], buffer, chunksize);

      if (readsize != chunksize) {
        fprintf(stderr, "ERROR: Requested %d bytes, read %d\n", chunksize, readsize);
        readsize = chunksize;
      }
      for (k = 0; k < readsize; k++) {
        if (buffer[k] != (char)(j+k)) {
          fprintf(stderr, "ERROR: data error at offset %d in file %s (%c,%c)\n",
                  j+k, names[i], buffer[k], (char)(j+k));
          error_count++;
          break;
        }
      }
      free(buffer);
    }
  }

  for (i = 0; i < 2; i++) {
    if (sfs_fclose(fds[i]) != 0) {
      fprintf(stderr, "ERROR: closing file %s\n", names[i]);
      error_count++;
    }
  }

  /* Now try to close the files. Don't
   * care about the return codes, really, but just want to make sure
   * this doesn't cause a problem.
   */
  for (i = 0; i < 2; i++) {
    if (sfs_fclose(fds[i]) == 0) {
      fprintf(stderr, "Warning: closing already closed file %s\n", names[i]);
    }
  }

  /* Now just try to open up a bunch of files.
   */
  ncreate = 0;
  for (i = 0; i < MAX_FD; i++) {
    names[i] = rand_name();
    fds[i] = sfs_fopen(names[i]);
    if (fds[i] < 0) {
      break;
    }
    sfs_fclose(fds[i]);
    ncreate++;
  }

  printf("Created %d files in the root directory\n", ncreate);

  nopen = 0;
  for (i = 0; i < ncreate; i++) {
    fds[i] = sfs_fopen(names[i]);
    if (fds[i] < 0) {
      break;
    }
    nopen++;
  }
  printf("Simultaneously opened %d files\n", nopen);

  for (i = 0; i < nopen; i++) {
    tmp = sfs_fwrite(fds[i], test_str, strlen(test_str));
    if (tmp != strlen(test_str)) {
      fprintf(stderr, "ERROR: Tried to write %d, returned %d\n", 
              (int)strlen(test_str), tmp);
      error_count++;
    }
    if (sfs_fclose(fds[i]) != 0) {
      fprintf(stderr, "ERROR: close of handle %d failed\n", fds[i]);
      error_count++;
    }
  }

  /* Re-open in reverse order */
  for (i = nopen-1; i >= 0; i--) {
    fds[i] = sfs_fopen(names[i]);
    if (fds[i] < 0) {
      fprintf(stderr, "ERROR: can't re-open file %s\n", names[i]);
    }
  }

  /* Now test the file contents.
   */
  for (i = 0; i < nopen; i++) {
      sfs_fseek(fds[i], 0);
  }

  for (j = 0; j < strlen(test_str); j++) {
    for (i = 0; i < nopen; i++) {
      char ch;

      if (sfs_fread(fds[i], &ch, 1) != 1) {
        fprintf(stderr, "ERROR: Failed to read 1 character\n");
        error_count++;
      }
      if (ch != test_str[j]) {
        fprintf(stderr, "ERROR: Read wrong byte from %s at %d (%c,%c)\n", 
                names[i], j, ch, test_str[j]);
        error_count++;
        break;
      }
    }
  }

  /* Now close all of the open file handles.
   */
  for (i = 0; i < nopen; i++) {
    if (sfs_fclose(fds[i]) != 0) {
      fprintf(stderr, "ERROR: close of handle %d failed\n", fds[i]);
      error_count++;
    }
  }

  /* Now we try to re-initialize the system.
   */
  mksfs(0);

  for (i = 0; i < nopen; i++) {
    fds[i] = sfs_fopen(names[i]);
    sfs_fseek(fds[i], 0);
    if (fds[i] >= 0) {
      readsize = sfs_fread(fds[i], fixedbuf, sizeof(fixedbuf));
      if (readsize != strlen(test_str)) {
        fprintf(stderr, "ERROR: Read wrong number of bytes\n");
        error_count++;
      }

      for (j = 0; j < strlen(test_str); j++) {
        if (test_str[j] != fixedbuf[j]) {
          fprintf(stderr, "ERROR: Wrong byte in %s at %d (%c,%c)\n", 
                  names[i], j, fixedbuf[j], test_str[j]);
          printf("%d\n", fixedbuf[1]);
          error_count++;
          break;
        }
      }

      if (sfs_fclose(fds[i]) != 0) {
        fprintf(stderr, "ERROR: close of handle %d failed\n", fds[i]);
        error_count++;
      }
    }
  }

  printf("Trying to fill up the disk with repeated writes to %s.\n", names[0]);
  printf("(This may take a while).\n");

  /* Now try opening the first file, and just write a huge bunch of junk.
   * This is just to try to fill up the disk, to see what happens.
   */
  fds[0] = sfs_fopen(names[0]);
  if (fds[0] >= 0) {
    for (i = 0; i < 100000; i++) {
      int x;

      if ((i % 100) == 0) {
        fprintf(stderr, "%d\r", i);
      }

      memset(fixedbuf, (char)i, sizeof(fixedbuf));
      x = sfs_fwrite(fds[0], fixedbuf, sizeof(fixedbuf));
      if (x != sizeof(fixedbuf)) {
        /* Sooner or later, this write should fail. The only thing is that
         * it should fail gracefully, without any catastrophic errors.
         */
        printf("Write failed after %d iterations.\n", i);
        printf("If the emulated disk contains just over %d bytes, this is OK\n",
               (i * (int)sizeof(fixedbuf)));
        break;
      }
    }
    sfs_fclose(fds[0]);
  }
  else {
    fprintf(stderr, "ERROR: re-opening file %s\n", names[0]);
  }

  /* Now, having filled up the disk, try one more time to read the
   * contents of the files we created.
   */
  for (i = 0; i < nopen; i++) {
    fds[i] = sfs_fopen(names[i]);
    sfs_fseek(fds[i], 0);
    if (fds[i] >= 0) {
      readsize = sfs_fread(fds[i], fixedbuf, sizeof(fixedbuf));
      if (readsize < strlen(test_str)) {
        fprintf(stderr, "ERROR: Read wrong number of bytes\n");
        error_count++;
      }

      for (j = 0; j < strlen(test_str); j++) {
        if (test_str[j] != fixedbuf[j]) {
          fprintf(stderr, "ERROR: Wrong byte in %s at position %d (%c,%c)\n", 
                  names[i], j, fixedbuf[j], test_str[j]);
          error_count++;
          break;
        }
      }

      if (sfs_fclose(fds[i]) != 0) {
        fprintf(stderr, "ERROR: close of handle %d failed\n", fds[i]);
        error_count++;
      }
    }
  }

  for(i = 2; i < nopen; i++) {
      if( sfs_GetFileSize(names[i]) != strlen(test_str)) {
          printf("Error: %s not correct size of %d bytes: %d\n", names[i], strlen(test_str), sfs_GetFileSize(names[i]));
          error_count++;
      }
  }
  
  i = 0;
  sfs_get_next_filename(file_name);
  sfs_get_next_filename(file_name);
  
  while(sfs_get_next_filename(file_name)) {
      if(strcmp(file_name, names[i]) != 0) {
          printf("file names do not match %s and %s\n", file_name, names[i]);
          error_count++;
      }
      i++;
  }
  
  printf("Attempting to remove %s\n", names[5]);
  sfs_remove(names[5]);
  
  i = 0; j = 0;
  while(sfs_get_next_filename(file_name)) {
      if(strcmp(file_name, names[5]) == 0) {
          printf("file not removed\n");
          error_count++;
      } else if(strcmp(names[6], file_name) == 0) j = 1;
  }
  
  if(j != 1) {
      printf("file not found\n");
      error_count++;
  }
  
  fprintf(stderr, "Test program exiting with %d errors\n", error_count);
  return (error_count);
}


















                                                                                                                                                                                                              Assign_3/disk_emu.h                                                                                 0000664 0001750 0001750 00000000425 12437420610 013151  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   int init_fresh_disk(char *filename, int block_size, int num_blocks);
int init_disk(char *filename, int block_size, int num_blocks);
int read_blocks(int start_address, int nblocks, void *buffer);
int write_blocks(int start_address, int nblocks, void *buffer);
int close_disk();
                                                                                                                                                                                                                                           Assign_3/sfs_api.h                                                                                  0000664 0001750 0001750 00000000545 12506624634 013011  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   #define MAXFILENAME 21

int mksfs(int fresh);
int sfs_fopen(char *name);
int sfs_fclose(int fileID);
int sfs_fwrite(int fileID, const char *buf, int length);
int sfs_fread(int fileID, char *buf, int length);
int sfs_fseek(int fileID, int offset);
int sfs_remove(char *file);
int sfs_get_next_filename(char* filename);
int sfs_GetFileSize(const char* path);
                                                                                                                                                           Assign_3/Makefile                                                                                   0000664 0001750 0001750 00000000601 12507122521 012632  0                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                   CFLAGS = -c -Wall `pkg-config fuse --cflags --libs`
#CFLAGS+= -g
LDFLAGS = `pkg-config fuse --cflags --libs`

SOURCES= disk_emu.c sfs_api.c fuse_wrappers.c
#SOURCES= disk_emu.c sfs_api.c sfs_test.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=sfs

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	gcc $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	gcc $(CFLAGS) $< -o $@

clean:
	rm -rf *.o *~ 
                                                                                                                               Assign_3/mnt/                                                                                       0000775 0001750 0001750 00000000000 12476660110 012001  5                                                                                                    ustar   anrl                            anrl                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   