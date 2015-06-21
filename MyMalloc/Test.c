/* 
 * File:   Test.c
 * Author: Ahmed Youssef
 * 
 */

#include <stdio.h>
#include <string.h>
#include "malloc_api.h"

#define BLOCKSIZ 10
#define NUMPOINTERS 10

int main() 
{
    unsigned char* data;
    int i, error=0;
    char* ptr[NUMPOINTERS];
    
    printf("\nData Integrity Test\n");
    my_mallinfo();
    data = (unsigned char*) my_malloc(BLOCKSIZ);
    if(data == NULL) printf("%s", my_malloc_error);
    my_mallinfo();
    
    for(i = 0; i < BLOCKSIZ; i++) {
        data[i] = i;
    }
    
    for(i = 0; i < BLOCKSIZ; i++) {
        if(data[i] != i) { 
            printf("i = %d\n", i);
            error++;
        }
    }
    
    printf("\nError count = %d\n", error);
    
    my_free(data);
    
    printf("\nAfter Freeing data\n");
    my_mallinfo();
    
    for(i = 0; i < NUMPOINTERS; i++) {
        ptr[i] = my_malloc((NUMPOINTERS-i)*BLOCKSIZ);
        if(ptr == NULL) printf("%s", my_malloc_error);
    }
    
    printf("\nAfter Allocating Pointers\n");
    my_mallinfo();
    
    my_free(ptr[0]);
    my_free(ptr[1]);
    
    printf("\nAfter Freeing First two Pointers (Merge Test)\n");
    my_mallinfo();
    
    my_free(ptr[3]);
    my_free(ptr[9]);
    
    printf("\nAfter Freeing two Pointers\n");
    my_mallinfo();
    
    ptr[0] = (char*) my_malloc(BLOCKSIZ);
    
    printf("\nAfter mallocing (First Fit test)\n");
    my_mallinfo();
    
    my_free(ptr[7]);
    printf("\nAfter freeing\n");
    my_mallinfo();
    
    my_mallopt(BF_POLICY);
    
    ptr[1] = (char*) my_malloc(BLOCKSIZ);
    printf("\nAfter mallocing again(Best Fit Test)\n");
    my_mallinfo();
    
    ptr[3] = my_malloc(136);
    printf("\nAfter mallocing big block\n");
    my_mallinfo();
    
    my_free(ptr[3]);
    printf("\nAfter freeing last block (Decrease Heap Test)\n");
    my_mallinfo();
    
    return 0;
}
