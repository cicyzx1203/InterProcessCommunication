The overall design is descripted as follows:
1. The proxy and the cache build a command channel (cmd_chl) through cmd_snd_ini() and cmd_rcv_ini() respedctively. And make handshake to get prepared to communicate with each other.

2.(1) For cache, it creates a request queue for storing requests got from the proxy, and creates multi-threads. Worker threads get a request everytime by using mutex (in workercb function).
  (2) For proxy, it initializes n segments in shared memory and also create multi-threads for handling data reading.

3. As both proxy and cache are mapped into the same shared memory, then we can starting dealing with requests, data writing and writing (by implementing semaphore):
  (1) For cache, (in handle_req function)if FILE_FOUND, it writes the requested data into the shared segment chunk by chunk.
  (2) For proxy, (in handle_with_file.c)if FILE_FOUND, it reads the data from the shared segment chunk by chunk.


