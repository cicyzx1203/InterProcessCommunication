#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/signal.h>
#include <printf.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/mman.h>

#include "gfserver.h"
#include "steque.h"
#include "shm_channel.h"

#define BUFSIZE (4096)

pthread_mutex_t shmq_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  shmq_cond  = PTHREAD_COND_INITIALIZER;
steque_t        shmq;

void shm_init(unsigned int num_seg, unsigned int segsize);
void cleanup();
shm_t* shm_deq();
void shm_enq(shm_t*);

ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg)
{
    struct timespec ts;
    char buffer[BUFSIZE];
    strcpy(buffer, path);

    mqd_t cmd_chl = (mqd_t)((intptr_t)arg);
    shm_t* shm = shm_deq();

    char* shmnm = shm->seg_name;
    size_t shmsz = shm->seg_size;

    sem_t* semr = sem_create(shm->seg_name, READER);
    sem_t* semw = sem_create(shm->seg_name, WRITER);

    req_send(cmd_chl, buffer, shmnm, shmsz);

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 20;
    if (-1 == sem_timedwait(semr, &ts))
        exit(SERVER_FAILURE);

    cache_t* pcache  = shm_getdata(shm);
    size_t file_size = pcache->file_size;
    status_t status  = pcache->status;

    if (status == FILE_NOT_FOUND)
    {
        shm_enq(shm);
        sem_post(semw);
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }
    
    gfs_sendheader(ctx, GF_OK, file_size);
    sem_post(semw);

    size_t transferred = 0;
    void* data = cache_get_data(pcache);
    while(transferred < file_size) 
    {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 20;
        if (-1 == sem_timedwait(semr, &ts))
            exit(SERVER_FAILURE);

        size_t chksz    = pcache->chunk_size;
        ssize_t written = gfs_send(ctx, data, chksz);
        if (written != chksz) 
        {e
            shm_enq(shm);
            sem_post(semw);
            return -1;
        }
        transferred += written;
        sem_post(semw);
    }
    shm_enq(shm);
    sem_post(semw);
    return transferred;
}

void shm_init(unsigned int num_seg, unsigned int segsize) 
{
    shm_t* pseg;

    steque_init(&shmq);
    for (int segnum = 0; segnum < num_seg; ++segnum) 
        if (NULL != (pseg = create_shm(segnum, segsize))) 
            steque_enqueue(&shmq, pseg);
}

shm_t* shm_deq() 
{
    pthread_mutex_lock(&shmq_mutex);
    while(steque_isempty(&shmq))
        pthread_cond_wait(&shmq_cond, &shmq_mutex);
    shm_t* shm = steque_pop(&shmq);
    pthread_mutex_unlock(&shmq_mutex);
    return shm;
}

void shm_enq(shm_t* shm) 
{
    pthread_mutex_lock(&shmq_mutex);
    steque_enqueue(&shmq, shm);
    pthread_mutex_unlock(&shmq_mutex);
    pthread_cond_signal(&shmq_cond);
}

void cleanup() 
{
    pthread_mutex_lock(&shmq_mutex);
    while (!steque_isempty(&shmq)) 
    {
        const char* shmnm = ((shm_t *)steque_pop(&shmq))->seg_name;
        shm_unlink(shmnm);
        sem_unlink(shmnm);
        mq_unlink(CMD_MSG_Q);
    }
    pthread_mutex_unlock(&shmq_mutex);
    pthread_cond_destroy(&shmq_cond);
    pthread_mutex_destroy(&shmq_mutex);
}
