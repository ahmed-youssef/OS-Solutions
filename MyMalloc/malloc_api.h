/* 
 * File:   malloc_api.h
 * Author: Ahmed Youssef
 *
 */

#ifndef MALLOC_API_H
#define	MALLOC_API_H

#define FF_POLICY 0
#define BF_POLICY 1
#define MAXERRORSIZE 100

void* my_malloc(int  size);
void my_free(void  *ptr);
void my_mallopt(int  policy);
void my_mallinfo();

extern char my_malloc_error[MAXERRORSIZE];

#endif	/* MALLOC_API_H */

