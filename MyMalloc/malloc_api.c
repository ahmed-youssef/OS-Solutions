/*
 * COMP 310/ECSE 427
 * Winter 2015
 * Programming Assignment 2 Solution
 * Author: Ahmed Youssef
 * malloc_api.c
 * malloc implemenentation using sbrk(). 
	Supports best fit and first fit allocation.
	Merges consective free blocks.
	Decreases program break when last block is free and greather than LASTBLKLIMIT bytes
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "malloc_api.h"

// Maximum size of last free block at the end of the heap
#define LASTBLKLIMIT 128  // 128 bytes chosen instead of 128 KB
#define USED 1
#define FREE 0

typedef struct tag_i {
    int length;
    int status;
}tag_t;

typedef struct free_block_i free_block_t;
struct free_block_i {
    tag_t start_tag;
    free_block_t* prev;
    free_block_t* next;
};

tag_t* get_free_block(int size);
void set_end_tag(tag_t* start_tag);
tag_t* get_end_tag(tag_t* start_tag);
tag_t* increase_heap_size(int size);
int decrease_heap_size(free_block_t* last_free_block);
void remove_from_ll(free_block_t* free_block);
void insert_to_ll(free_block_t* free_block);
void merge_free_blocks(free_block_t* free_block);
void merge_blocks(free_block_t* first_block, free_block_t* scnd_block);

free_block_t* free_list_head = NULL;
char my_malloc_error[MAXERRORSIZE];
int alloc_policy = FF_POLICY;
int total_alloc_bytes = 0;
int total_free_bytes = 0;

void* my_malloc(int size)
{
    tag_t* start_tag;
    
    start_tag = get_free_block(size);
    
    if(start_tag == NULL) {
        start_tag = increase_heap_size(size);
        if(start_tag == NULL) {
            strcpy(my_malloc_error, "Ran out of memory.\n");
            return NULL;
        }
    } else {
        total_free_bytes -= start_tag->length;
    }  
    
    total_alloc_bytes += start_tag->length;
    return (void*)&start_tag[1];
}

void my_free(void  *ptr)
{
    free_block_t* free_block =  (free_block_t*)((char*)ptr - sizeof(tag_t));
    int dec_amount;
    
    total_alloc_bytes -= free_block->start_tag.length;
    total_free_bytes += free_block->start_tag.length;
    insert_to_ll(free_block);
    
    if((free_block->next == NULL) && (free_block->start_tag.length > LASTBLKLIMIT)) {
        dec_amount = decrease_heap_size(free_block);
        total_free_bytes -= dec_amount;
    }  
}

void my_mallopt(int  policy)
{
    alloc_policy = policy;
}

void my_mallinfo()
{
    free_block_t* free_block;
    int largest_free_space=0;
    
    // Get Largest Free Space
    for(free_block = free_list_head; free_block != NULL; free_block = free_block->next) {
        if(free_block->start_tag.length > largest_free_space) {
            largest_free_space = free_block->start_tag.length;
        }      
    }
    
    printf("Total Bytes Allocated = %d\n", total_alloc_bytes);
    printf("Total bytes of free space = %d\n", total_free_bytes);
    printf("Largest Contiguous Free Space = %d\n", largest_free_space);
}

void insert_to_ll(free_block_t* free_block)
{
    free_block_t* curr_block, *next_block;
    
    if(free_list_head == NULL)
    {
        free_list_head = free_block;
        free_list_head->prev = NULL;
        free_list_head->next = NULL;    
    } else if(free_block < free_list_head)
    {
        free_list_head->prev = free_block;
        free_block->prev = NULL;
        free_block->next = free_list_head;
        free_list_head = free_block;
    } else
    {
        for(curr_block = free_list_head; curr_block->next != NULL; curr_block = curr_block->next) {
            next_block = curr_block->next;
            if(free_block < next_block) {
                curr_block->next = free_block;
                next_block->prev = free_block;
                free_block->prev = curr_block;
                free_block->next = next_block;
                break;
            }
        }
        if(curr_block->next == NULL) {
            curr_block->next = free_block;
            free_block->prev = curr_block;
            free_block->next = NULL;
        }
    }
    
    free_block->start_tag.status = FREE;
    set_end_tag(&(free_block->start_tag));
    merge_free_blocks(free_block);
}

void remove_from_ll(free_block_t* free_block)
{
    free_block_t* prev_block, *next_block;
    
    if(free_block == free_list_head) 
    {
        free_list_head = free_block->next;
        if(free_list_head != NULL) free_list_head->prev = NULL;
    } else if(free_block->next == NULL)
    {
        prev_block = free_block->prev;
        prev_block->next = NULL;
    } else {
        prev_block = free_block->prev;
        next_block = free_block->next;
        
        prev_block->next = next_block;
        next_block->prev = prev_block;
    }
}

tag_t* get_free_block(int size)
{
    free_block_t* curr_block, *free_block = NULL;
   
    if(alloc_policy == FF_POLICY) {
        for(curr_block = free_list_head; curr_block != NULL; curr_block = curr_block->next) {
            if(curr_block->start_tag.length >= size) {
                free_block = curr_block;
                break;
            }      
        }
    } else {
        for(curr_block = free_list_head; curr_block != NULL; curr_block = curr_block->next) {
            if(curr_block->start_tag.length >= size) {
                if(free_block == NULL) {
                    free_block = curr_block;
                } else if(curr_block->start_tag.length < free_block->start_tag.length) {
                    free_block = curr_block;
                }
            }      
        }
    }
    
    if(free_block != NULL) {
        remove_from_ll(free_block);
        free_block->start_tag.status = USED;
        set_end_tag(&(free_block->start_tag));
    }
    
    return &(free_block->start_tag);
}

tag_t* increase_heap_size(int size)
{
    tag_t* start_tag;
    
    start_tag = sbrk(sizeof(tag_t)*2 + size);
    if(start_tag == (void*)-1) return NULL;
    
    start_tag->length = size;
    start_tag->status = USED;
    set_end_tag(start_tag);
    
    return start_tag;
}

int decrease_heap_size(free_block_t* last_free_block)
{
    char* last_heap_addr, *last_block_addr;
    tag_t* end_tag;
    int dec_amount = 0;
    last_heap_addr = sbrk(0);
    
    end_tag = get_end_tag(&(last_free_block->start_tag));
    last_block_addr = (char*)(&end_tag[1]);
    
    // Check to see if there are no malloced data blocks after it
    if(last_block_addr == last_heap_addr) {
        dec_amount = ((last_free_block->start_tag.length)/LASTBLKLIMIT)*LASTBLKLIMIT;
        sbrk(-dec_amount);
        last_free_block->start_tag.length -= dec_amount;
        set_end_tag(&(last_free_block->start_tag));
    }
    
    return dec_amount;
}

void set_end_tag(tag_t* start_tag)
{
    tag_t* end_tag;
    
    end_tag = get_end_tag(start_tag);
    end_tag->length = start_tag->length;
    end_tag->status = start_tag->status; 
}

tag_t* get_end_tag(tag_t* start_tag)
{
    tag_t* end_tag;
    char* data;
    
    data = (char*)&start_tag[1];
    
    end_tag = (tag_t*) (&data[start_tag->length]);
    return end_tag; 
}

void merge_free_blocks(free_block_t* free_block)
{
    tag_t* tag;
    
    if(free_block != free_list_head) {
        // Get end tag of previous block
        tag = (tag_t*) free_block;
        tag--;

        if(tag->status == FREE)
            merge_blocks(free_block->prev, free_block);
    }
    
    if(free_block->next != NULL) {
        // Get start tag of next block
        tag = get_end_tag(&(free_block->start_tag));
        tag++;

        if(tag->status == FREE)
            merge_blocks(free_block, free_block->next);
    }
}

void merge_blocks(free_block_t* first_block, free_block_t* scnd_block)
{
    first_block->next = scnd_block->next;
    first_block->start_tag.length += (scnd_block->start_tag.length + sizeof(tag_t)*2);
    
    // We gain two tags of free space from the merge
    total_free_bytes += sizeof(tag_t)*2;
    set_end_tag(&(first_block->start_tag));
}



