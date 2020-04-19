#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <curl/curl.h>

#include "gfserver.h"

#define USAGE                                                                         \
"usage:\n"                                                                            \
"  webproxy [options]\n"                                                              \
"options:\n"                                                                          \
"  -p [listen_port]    Listen port (Default: 51418)\n"                                \
"  -s [server]         The server to connect to (Default: Udacity S3 instance)\n"     \
"  -x                  Experimental Option (Bonnie Only)\n"                           \
"  -t [thread_count]   Num worker threads (Default: 1, Range: 1-1024)\n"              \
"  -h                  Show this help message\n"


/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"port",          required_argument,      NULL,           'p'},
  {"server",        required_argument,      NULL,           's'},
  {"thread-count",  required_argument,      NULL,           't'},
  {"help",          no_argument,            NULL,           'h'},
  {NULL,            0,                      NULL,            0}
};

extern ssize_t handle_with_file(gfcontext_t *ctx, const char *path, void* arg);
extern ssize_t handle_with_curl(gfcontext_t *ctx, const char *path, void* arg);
extern ssize_t handle_with_cache(gfcontext_t *ctx, const char *path, void* arg);

static gfserver_t gfs;

static void _sig_handler(int signo){
  if (signo == SIGTERM || signo == SIGINT){
    gfserver_stop(&gfs);
    exit(signo);
  }
}

int main(int argc, char **argv) {
  int i;
  int option_char = 0;
  unsigned short port = 51418;
  unsigned short nworkerthreads = 1;
  const char *server = "s3.amazonaws.com/content.udacity-data.com";
  CURLcode cg_init;

  // disable buffering on stdout so it prints immediately 
  setbuf(stdout, NULL);

  if (signal(SIGINT, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    exit(SERVER_FAILURE);
  }

  if (signal(SIGTERM, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    exit(SERVER_FAILURE);
  }

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:hxs:t:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;
      case 'x': // Bonnie only
        break;
      case 's': // file-path
        server = optarg;
        break;              
      case 't': // thread-count
        nworkerthreads = atoi(optarg);
        break;
    }
  }


  if (NULL == server) {
    fprintf(stderr, "Invalid (null) server name\n");
    exit(__LINE__);
  }

  if ((nworkerthreads < 1) || (nworkerthreads > 1024)) {
    fprintf(stderr, "Invalid number of worker threads\n");
    exit(__LINE__);
  }

  if (port < 1024) {
    fprintf(stderr, "Invalid port number\n");
    exit(__LINE__);
  }

  // This is where you initialize your shared memory 
  cg_init = curl_global_init(CURL_GLOBAL_ALL);
  if (cg_init != 0)
  {
    fprintf(stderr, "Shared memory initialization failure.\n");
    exit(__LINE__);
  }

  // This is where you initialize the server struct
  gfserver_init(&gfs, nworkerthreads);

  // This is where you set the options for the server 
  gfserver_setopt(&gfs, GFS_PORT, port);
  gfserver_setopt(&gfs, GFS_MAXNPENDING, 12);
  gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_curl);
  for(i = 0; i < nworkerthreads; i++) {
    gfserver_setopt(&gfs, GFS_WORKER_ARG, i, server);
  }
  
  // This is where you invoke the framework to run the server 
  // Note that it loops forever
  gfserver_serve(&gfs);

  // not reached

}
