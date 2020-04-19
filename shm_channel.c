#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "shm_channel.h"

void* shm_getdata(shm_t *shm) 
{
    return ((void*)shm + sizeof(shm_t));
}

void* cache_init(shm_t* shm) 
{
    cache_t* cache_data    = shm_getdata(shm);
    cache_data->status     = FILE_NOT_FOUND;
    cache_data->file_size  = 0;
    cache_data->cache_size = shm->seg_size - sizeof(shm_t) - 
                             sizeof(cache_t);
    return cache_data;
}

void* cache_get_data(cache_t *cache) 
{
    return ((void*)cache + sizeof(cache_t));
}

shm_t* create_shm(unsigned int segnum, unsigned int segsz) 
{
    int    shmfd;
    char   segName[NAME_LEN] = {0};
    shm_t* pseg              = NULL;

    snprintf(segName, NAME_LEN, "/data_shm_%d", segnum);
    shmfd = shm_open(segName, O_CREAT|O_RDWR, S_IRWXU|S_IRWXG);
    if (shmfd < 0) 
        goto done;

    ftruncate(shmfd, sizeof(shm_t) + segsz);

    pseg = mmap(0, sizeof(shm_t) + segsz, (PROT_READ|PROT_WRITE),
                MAP_SHARED, shmfd, 0);
    close(shmfd);
    if (pseg == MAP_FAILED) 
    {
        pseg = NULL;
        goto done;
    }

    strncpy(pseg->seg_name, segName, NAME_LEN-1);
    pseg->seg_name[NAME_LEN-1] = '\0';
    pseg->seg_size = segsz;

  done:
    return pseg;
}

shm_t* get_shmseg(const char* shmnm, size_t shmsz) 
{
    shm_t *pseg = NULL;
    
    int   shmfd = shm_open(shmnm, O_RDWR, S_IRWXU|S_IRWXG);
    if (shmfd < 0) 
        goto done;

    ftruncate(shmfd, sizeof(shm_t) + shmsz);

    pseg = mmap(0, sizeof(shm_t) + shmsz, PROT_READ|PROT_WRITE,
                MAP_SHARED, shmfd, 0);
    close(shmfd);
    if (pseg == MAP_FAILED) 
    {
        pseg = NULL;
        goto done;
    }

    strncpy(pseg->seg_name, shmnm, NAME_LEN-1);
    pseg->seg_name[NAME_LEN-1] = '\0';
    pseg->seg_size = shmsz;

  done:
    return pseg;
}

mqd_t cmd_snd_ini() 
{
    struct mq_attr attr;
    req_t          req;

    attr.mq_msgsize = sizeof(req_t);
    attr.mq_maxmsg  = 8;
    attr.mq_curmsgs = 0;
    attr.mq_flags   = 0;

    mqd_t cmd_chl = mq_open(CMD_MSG_Q, O_RDWR|O_CREAT,
                            S_IRWXU|S_IRWXG|S_IRWXO, &attr);

    if (cmd_chl != (mqd_t)-1)
    {
        req.cmd_type = SYNC;
        mq_send(cmd_chl, (const char*)&req, sizeof(req), 0); 
    }

    return cmd_chl;
}

mqd_t cmd_rcv_ini() 
{
    struct mq_attr attr;
    req_t          req;

    attr.mq_msgsize = sizeof(req_t);
    attr.mq_maxmsg  = 8;
    attr.mq_curmsgs = 0;
    attr.mq_flags   = 0;

    mqd_t cmd_chl = -1;
    while ((mqd_t)-1 == (cmd_chl = mq_open(CMD_MSG_Q, O_RDWR,
                                         S_IRWXU|S_IRWXG|S_IRWXO, &attr)))
        sleep(3);
    mq_receive(cmd_chl, (char*)&req, sizeof(req), NULL);

    return cmd_chl;
}

req_t* get_request(mqd_t cmd_chl) 
{
    req_t* req = calloc(1, sizeof(req_t));
    if (-1 != mq_receive(cmd_chl, (char*)req, sizeof(req_t), NULL))
        return req;

    free(req);
    return NULL;
}

void req_send(mqd_t cmd_chl, const char* path, const char* shmnm,
              size_t shmsz) 
{
    req_t req;
    req.cmd_type = GET;
    req.shm_size = shmsz;

    strcpy(req.seg_name, shmnm);
    strcpy(req.path, path);

    mq_send(cmd_chl, (const char*)&req, sizeof(req), 0);
}

sem_t* sem_create(const char* shmnm, semTyp semType) 
{
    int  init_val;
    char sem_name[32] = {0};

    strcpy(sem_name, shmnm);

    if (semType == READER) 
    {
        init_val = 0;
        strcat(sem_name, "_reader");
    }
    else 
    {
        init_val = 1;
        strcat(sem_name, "_writer");
    }

    sem_t* sem = sem_open(sem_name, O_RDWR|O_CREAT, 
                          S_IRWXU|S_IRWXG|S_IRWXO, init_val);
    return sem;
}

sem_t* get_sem(const char* shmnm, semTyp semType) 
{
    char sem_name[NAME_LEN + 8] = {0};

    strcpy(sem_name, shmnm);

    if (semType == READER) 
        strcat(sem_name, "_reader");
    else 
        strcat(sem_name, "_writer");
  
    sem_t* sem = sem_open(sem_name, O_RDWR, 
                          S_IRWXU|S_IRWXG|S_IRWXO, 0);
    return sem;
}

void cleanup_msg()
{
    mq_unlink(CMD_MSG_Q);
}
