#ifndef SHM_CHANNEL_H
#define SHM_CHANNEL_H

#include <sys/types.h>
#include <mqueue.h>
#include <semaphore.h>

#define NAME_LEN (64)
#define CMD_MSG_Q "/cmd_msg_q"

typedef enum { FILE_NOT_FOUND, FILE_FOUND, ERROR } status_t;
typedef struct cache_t
{
  status_t status; 
  size_t   file_size;
  size_t   cache_size;
  volatile size_t chunk_size;
} cache_t;

typedef struct shm_t 
{
  char   seg_name[NAME_LEN];
  size_t seg_size;
} shm_t;

#define MAX_REQUEST_LEN 128
typedef enum { READER, WRITER } semTyp;
typedef enum { SYNC, ACK, GET } cmdTyp;

typedef struct req_t 
{
  size_t shm_size;
  char   seg_name[NAME_LEN];
  char   path[MAX_REQUEST_LEN];
  cmdTyp cmd_type;
} req_t;

void* shm_getdata(shm_t *);

void* cache_init(shm_t*);
void* cache_get_data(cache_t *);
void  cache_set_data(cache_t *, void*);

shm_t* create_shm(unsigned int segnum, unsigned int segsz);

shm_t* get_shmseg(const char* shmnm, size_t shmsz);


req_t* get_request(mqd_t);
void req_send(mqd_t, const char* path, const char* shmnm, size_t shmsz);

sem_t* sem_create(const char* shmnm, semTyp);
sem_t* get_sem(const char* shmnm, semTyp);

mqd_t cmd_snd_ini();
mqd_t cmd_rcv_ini();

void cleanup_msg();

#endif
