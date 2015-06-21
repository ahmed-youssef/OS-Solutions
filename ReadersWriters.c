/*
COMP 310/ECSE 427
Winter 2015
Readers-Writers
Assignment 2 Solution
Author: Ahmed Youssef
*/ 

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>

#define MAXREADERS 100
#define MAXWRITERS 10
#define MAXTIME 100

int shared_variable = 0;
int read_count = 0;
sem_t rw_mutex;
sem_t mutex;
int iteration_num;

typedef struct thread_data {
	pthread_t thread;
	double avg_time;
	double max_time;
	double min_time;
} thread_data_t;

thread_data_t reader[MAXREADERS];
thread_data_t writer[MAXWRITERS];

void get_avg(thread_data_t* rw_thread, int limit, double* global_avg,
												  double* global_max,
												  double* global_min);
static void *Reader_thread(void* arg) 
{
    int local_variable, i, id;
    struct timeval  tv1, tv2;
    double exec_time;

	id = (int)arg;
	
	sem_wait(&mutex);
	read_count++;
	if (read_count == 1) { 
	    gettimeofday(&tv1, NULL);
	    sem_wait(&rw_mutex);
	    gettimeofday(&tv2, NULL);
	}
	sem_post(&mutex);
	
	local_variable = shared_variable;

	sem_wait(&mutex);
	read_count--;
	if(read_count == 0) sem_post(&rw_mutex);
	sem_post(&mutex);

	exec_time = (double)( (tv2.tv_usec - tv1.tv_usec) / 1000. +
         (double) (tv2.tv_sec - tv1.tv_sec) * 1000.);
	
	reader[id].min_time = exec_time; 
	reader[id].max_time = exec_time; 
	reader[id].avg_time = exec_time;
	
	usleep(rand()%MAXTIME * 1000);

    for(i = 1; i < iteration_num; i++)
    {
		sem_wait(&mutex);
		read_count++;
		if (read_count == 1) { 
			gettimeofday(&tv1, NULL);	
			sem_wait(&rw_mutex);
			gettimeofday(&tv2, NULL);
			printf("Reader thread %d First to enter ----- rrree\n", id);
		}
		sem_post(&mutex);
		
		printf("Reader thread %d access shared varialble ----- rrre\n", id);
		local_variable = shared_variable;

		sem_wait(&mutex);
		read_count--;
		if(read_count == 0) {
			sem_post(&rw_mutex);
			printf("Reader thread %d last to leave ----- rrrll\n", id);
		}
		sem_post(&mutex);

		exec_time = (double)( (tv2.tv_usec - tv1.tv_usec) / 1000. +
		     (double) (tv2.tv_sec - tv1.tv_sec) * 1000.);
	
		if(exec_time > reader[id].max_time) reader[id].max_time = exec_time;
		if(exec_time < reader[id].min_time) reader[id].min_time = exec_time;
		reader[id].avg_time += exec_time;

		usleep(rand()%MAXTIME * 1000);
    }

    reader[id].avg_time /= (double)iteration_num;
}


static void *Writer_thread(void* arg)
{
    int i, id;
    struct timeval  tv1, tv2;
    double exec_time;

	id = (int)arg;
	
	gettimeofday(&tv1, NULL);
	sem_wait(&rw_mutex);
	gettimeofday(&tv2, NULL);

	shared_variable += 10;

	sem_post(&rw_mutex);

	exec_time = (double)( (tv2.tv_usec - tv1.tv_usec) / 1000. +
		     (double) (tv2.tv_sec - tv1.tv_sec) * 1000.);
	
	writer[id].min_time = exec_time; 
	writer[id].max_time = exec_time; 
	writer[id].avg_time = exec_time;

    for(i = 1; i < iteration_num; i++)
    {
		gettimeofday(&tv1, NULL);
		sem_wait(&rw_mutex);
		printf("Writer thread %d access shared varialble ----- wwwe\n", id);
		gettimeofday(&tv2, NULL);

		shared_variable+=10;

		sem_post(&rw_mutex);
		
		printf("Writer thread %d exitss shared varialble ----- wwwl\n", id);
		exec_time = (double)( (tv2.tv_usec - tv1.tv_usec) / 1000. +
		     (double) (tv2.tv_sec - tv1.tv_sec) * 1000.);

		if(exec_time > writer[id].max_time) writer[id].max_time = exec_time;
		if(exec_time < writer[id].min_time) writer[id].min_time = exec_time;
		writer[id].avg_time += exec_time;
		
		usleep(rand()%MAXTIME * 1000);
    }

	writer[id].avg_time /= (double)iteration_num;	
}


int main(int argc, char *argv[])
{
    int i;
	double readers_avg, readers_max, readers_min;
	double writers_avg, writers_max, writers_min;

    if(argc != 2) {
		printf("argument count is not 2\n");
		exit(1);
    }

    iteration_num = atoi(argv[1]);
    if(iteration_num <= 0) {
		printf("iteration number must by a positive number\n");
		exit(1);
    }

    sem_init(&rw_mutex, 0, 1);
    sem_init(&mutex, 0, 1);

    for(i = 0; i < MAXWRITERS; i++) {
        pthread_create(&writer[i].thread, NULL, &Writer_thread, (void*)i);
    }

    for(i = 0; i < MAXREADERS; i++) {
        pthread_create(&reader[i].thread, NULL, &Reader_thread, (void*)i);
    }
    	
    for(i = 0; i < MAXREADERS; i++) {
        pthread_join(reader[i].thread, NULL);
    }

    for(i = 0; i < MAXWRITERS; i++) {
        pthread_join(writer[i].thread, NULL);
    }
	
    printf("shared_variable = %d\n", shared_variable);

	get_avg(reader, MAXREADERS, &readers_avg,
								&readers_max,
								&readers_min);

	get_avg(writer, MAXWRITERS, &writers_avg,
								&writers_max,
								&writers_min);

	printf("Reader Threads' Stats:\n");
	printf("Average Waiting Time = %f ms\n", readers_avg);
	printf("Maximum Waiting Time = %f ms\n", readers_max);
	printf("Minumum Waiting Time = %f ms\n", writers_min);

	printf("Writer Threads' Stats:\n");
	printf("Average Waiting Time = %f ms\n", writers_avg);
	printf("Maximum Waiting Time = %f ms\n", writers_max);
	printf("Minumum Waiting Time = %f ms\n", writers_min);

    return 0;
}


void get_avg(thread_data_t* rw_thread, int limit, double* global_avg,
												  double* global_max,
												  double* global_min) 
{
	int i;

	*global_avg = rw_thread[0].avg_time;
	*global_max = rw_thread[0].max_time;
	*global_min = rw_thread[0].min_time;

	for(i = 1; i < limit; i++) 
	{
		*global_avg += rw_thread[i].avg_time; 
		if(*global_max < rw_thread[i].max_time) *global_max = rw_thread[i].max_time;
		if(*global_min > rw_thread[i].min_time) *global_min = rw_thread[i].min_time;
	}
	*global_avg /= limit;

}

