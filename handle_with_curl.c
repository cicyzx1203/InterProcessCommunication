#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <printf.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/signal.h>
#include <unistd.h>

#include "gfserver.h"

#define BUFSIZE (8803)

/*
 * Replace with an implementation of handle_with_curl and any other
 * functions you may need.
 */

typedef struct DataChunk{
    size_t  size;
    char   *memory;
} DataChunk;

size_t writecb(void *buf, size_t size, size_t nmemb, void *arg)
{
    DataChunk *chunk = (DataChunk *)arg;
    size_t     total = size * nmemb;

    chunk->memory = realloc(chunk->memory, chunk->size + total);
    if(!chunk->memory) 
    {
        printf("realloc failure\n");
        return 0;
    }

    memcpy(chunk->memory + chunk->size, buf, total);
    chunk->size += total;

    return total;
}

int send_data(gfcontext_t *ctx, DataChunk *data)
{
    ssize_t transferred = 0;
    ssize_t remained;
    ssize_t blk_len; 
    ssize_t sent_len;

    while(transferred < data->size)
    {
        remained = data->size - transferred;
        blk_len = (remained < BUFSIZE) ? remained : BUFSIZE;
        sent_len = gfs_send(ctx, data->memory + transferred, blk_len);
        if (sent_len != blk_len){
            fprintf(stderr, "gfs_send error");
            return EXIT_FAILURE;
        }
        transferred += sent_len;
    }
    return 0;
}

ssize_t handle_with_curl(gfcontext_t *ctx, char *path, void* arg)
{
    DataChunk  data;
    char       url[4096];
    CURL      *curl;
    CURLcode   curlcode;
    int        res;

    char      *base = arg;

    memset(&data, 0, sizeof(data));

    strcpy(url, base);
    strcat(url, path);

    curl = curl_easy_init();
    if (!curl) {
        return EXIT_FAILURE;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&data);
    curlcode = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (curlcode == 22) // file not found
    {
        free(data.memory);
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }

    if (curlcode) // other errors
    {
        free(data.memory);
        return EXIT_FAILURE;
    }

    // success
    gfs_sendheader(ctx, GF_OK, data.size);
    res = send_data(ctx, &data);
    free(data.memory);

    if (res)
        return EXIT_FAILURE;

    return data.size;
}

