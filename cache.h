// cache.h
#include <sys/time.h>

#include "csapp.h"

#define LIST_CNT 6
#define MAX_OBJECT_SIZE 102400

typedef struct cache_block {
    char *url;
    char *data;
    int datasize;
    int64_t timestamp;
    pthread_rwlock_t rwlock;
} cache_block;

/* allocate cache memory using calloc */
void cache_init();
/* free cache's memory */
void cache_deinit();
/* try to hit cache block and write content into fd, return 0 if failed */
int cache_read(char *url, int fd);
/* write content into free block or LRU block */
void cache_write(char *url, char *data, int len);
/* return current timestamp */
int64_t get_timestamp();