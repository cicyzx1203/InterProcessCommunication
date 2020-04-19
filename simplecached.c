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
#include <curl/curl.h>
#include <pthread.h>

#include "gfserver.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "steque.h"

#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif // CACHE_FAILURE

static void _sig_handler(int signo){
        if (signo == SIGTERM || signo == SIGINT){
                // you should do IPC cleanup here
                cleanup_msg();
                exit(signo);
        }
}

pthread_mutex_t req_q_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  req_q_cond  = PTHREAD_COND_INITIALIZER;
steque_t req_queue;

typedef struct worker_t {
    pthread_t thread_id;
} worker_t;

void workercb(void *args);
worker_t* workers_create(int nworkers);
void enq_req(mqd_t);
void handle_req(req_t*);

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work queue (Default: 3, Range: 1-31415)\n"      \
"  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"cachedir",           required_argument,      NULL,           'c'},
  {"nthreads",           required_argument,      NULL,           't'},
  {"help",               no_argument,            NULL,           'h'},
  {"hidden",                     no_argument,                    NULL,                   'i'}, /* server side */
  {NULL,                 0,                      NULL,             0}
};

void Usage() {
  fprintf(stdout, "%s", USAGE);
}

int main(int argc, char **argv) {
        int nthreads = 3;
        char *cachedir = "locals.txt";
        char option_char;

        /* disable buffering to stdout */
        setbuf(stdout, NULL);

        while ((option_char = getopt_long(argc, argv, "ic:ht:", gLongOptions, NULL)) != -1) {
                switch (option_char) {
                        default:
                                Usage();
                                exit(1);
                        case 'c': //cache directory
                                cachedir = optarg;
                                break;
                        case 'h': // help
                                Usage();
                                exit(0);
                                break;
                        case 't': // thread-count
                                nthreads = atoi(optarg);
                                break;
                        case 'i': // server side usage
                                break;
                }
        }

        if ((nthreads>31415) || (nthreads < 1)) {
                fprintf(stderr, "Invalid number of threads\n");
                exit(__LINE__);
        }

        if (SIG_ERR == signal(SIGINT, _sig_handler)){
                fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
                exit(CACHE_FAILURE);
        }

        if (SIG_ERR == signal(SIGTERM, _sig_handler)){
                fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
                exit(CACHE_FAILURE);
        }

    // Initialize cache
    simplecache_init(cachedir);

    mqd_t cmd_chl = cmd_rcv_ini();
    steque_init(&req_queue);
    worker_t* workers = workers_create(nthreads);

    while (1) 
    {
        enq_req(cmd_chl);
        pthread_cond_broadcast(&req_q_cond);
    }

    for(int i = 0; i < nthreads; ++i)
        pthread_join(workers[i].thread_id, NULL);

    free(workers);
    pthread_mutex_destroy(&req_q_mutex);
    pthread_cond_destroy(&req_q_cond);
    simplecache_destroy();
    return 0;
}

void workercb(void *args) 
{
    req_t *req;

    while(1) 
    {
        pthread_mutex_lock(&req_q_mutex);
        while (steque_isempty(&req_queue))
            pthread_cond_wait(&req_q_cond, &req_q_mutex);
        req = steque_pop(&req_queue);
        pthread_mutex_unlock(&req_q_mutex);

        if (req)
            handle_req(req);
    }
}

worker_t* workers_create(int nworkers) 
{
    worker_t* worker = calloc(nworkers, sizeof(worker_t));

    for (int i = 0; i < nworkers; ++i) 
        pthread_create(&worker[i].thread_id, NULL, (void *)&workercb,
                       &worker[i]);
    return worker;
}

void enq_req(mqd_t cmd_chl) 
{
    req_t* req = get_request(cmd_chl);
    if (req) 
    {
        pthread_mutex_lock(&req_q_mutex);
        steque_enqueue(&req_queue, req);
        pthread_mutex_unlock(&req_q_mutex);
    }
}

void handle_req(req_t* req)
{
    char  *shmnm = req->seg_name;
    size_t shmsz = req->shm_size;
    shm_t* shm   = get_shmseg(shmnm, shmsz);
    if (!shm)
        return;
    
    sem_t* semr = get_sem(shm->seg_name, READER);
    sem_t* semw = get_sem(shm->seg_name, WRITER);
    sem_wait(semw);
    cache_t* cache = cache_init(shm);

    char* path = req->path;
    int fd = simplecache_get(path);
    if (fd == -1) 
    {
        cache->status = FILE_NOT_FOUND;
        sem_post(semr);
        return;
    }

    size_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    cache->status = FILE_FOUND;
    cache->file_size = file_size;
    sem_post(semr);
    sem_wait(semw);

    size_t transferred = 0;
    void*  buffer      = cache_get_data(cache);
    size_t cachesize   = cache->cache_size;
    while (transferred < file_size)
    {
        size_t remained = (file_size - transferred);
        size_t requested = (cachesize > remained) ? remained :
                                                    cachesize;
        ssize_t read_len = read(fd, buffer, requested);
        if (read_len <= 0) 
        {
            cache->status = ERROR;
            sem_post(semr);
            return;
        }
        transferred += read_len;
        cache->chunk_size = read_len;
        sem_post(semr);
        sem_wait(semw);
    }
    free(req);
}
