#include "cache.h"

#include "csapp.h"

const int block_size[6] = {1024, 5120, 10240, 20480, 51200, 102400};
const int block_cnt[6] = {24, 10, 8, 6, 5, 5};

cache_block *cache_lists[LIST_CNT];

void cache_init() {
    for (int i = 0; i < LIST_CNT; ++i) {
        /* initialize cache block list */
        cache_lists[i] =
            (cache_block *)malloc(block_cnt[i] * sizeof(cache_block));
        cache_block *this_list = cache_lists[i];
        /* initialize every block in this list */
        for (int j = 0; j < block_cnt[i]; ++j) {
            this_list[j].url = (char *)calloc(MAXLINE, sizeof(char));
            this_list[j].data = (char *)calloc(block_size[i], sizeof(char));
            this_list[j].datasize = 0;
            this_list[j].timestamp = 0;
            pthread_rwlock_init(&this_list[j].rwlock, NULL);
        }
    }
}

void cache_deinit() {
    for (int i = 0; i < LIST_CNT; ++i) {
        cache_block *this_list = cache_lists[i];
        for (int j = 0; j < block_cnt[j]; ++j) {
            free(this_list[j].url);
            free(this_list[j].data);
            pthread_rwlock_destroy(&this_list[j].rwlock);
        }
        free(this_list);
    }
}

int cache_read(char *url, int fd) {
    /* search every list */
    int cache_hit = 0;
    cache_block *target = NULL;
    for (int i = 0; i < LIST_CNT; ++i) {
        cache_block *this_list = cache_lists[i];
        /* search every block in this list */
        for (int j = 0; j < block_cnt[i]; ++j) {
            /* if uri match, and timestamp not zero(means block valid), then
             * hit! */
            if (!strcmp(url, this_list[j].url) && this_list[j].timestamp) {
                cache_hit = 1;
                target = &this_list[j];
                break;
            }
        }
        if (cache_hit) break;
    }
    if (!cache_hit) {
        printf("no matched cache block\n");
        return 0;
    }

    /* first update timestamp before block kicked by other thread */
    pthread_rwlock_wrlock(&target->rwlock);
    /* we have to check target block again incase other thread kiked it */
    if (strcmp(url, target->url)) {
        printf("oops, the matched block modified by other thread just now\n");
        pthread_rwlock_unlock(&target->rwlock);
        return 0;
    }
    /* we can update the timestamp safely */
    target->timestamp = get_timestamp();
    pthread_rwlock_unlock(&target->rwlock);

    /* now we can get cache content */
    pthread_rwlock_rdlock(&target->rwlock);
    /* double check, just in case */
    if (strcmp(url, target->url)) {
        printf("oops, the matched block modified by other thread just now\n");
        pthread_rwlock_unlock(&target->rwlock);
        return 0;
    }
    Rio_writen(fd, target->data, target->datasize);
    pthread_rwlock_unlock(&target->rwlock);
    printf("fetch content from cache\n");
    return 1;
}

void cache_write(char *url, char *data, int len) {
    int list_idx = 0;
    cache_block *target = NULL;
    /* find target list */
    while ((list_idx < LIST_CNT) && (len > block_size[list_idx])) {
        ++list_idx;
    }
    if (list_idx == LIST_CNT) {
        printf("too much data to cache\n");
        return;
    }
    cache_block *this_list = cache_lists[list_idx];
    /* find free block or LRU block as target block */
    int64_t min_timestamp = get_timestamp();
    for (int j = 0; j < block_cnt[list_idx]; ++j) {
        if (this_list[j].timestamp < min_timestamp) {
            target = &this_list[j];
            min_timestamp = target->timestamp;
            if (!min_timestamp) break; /* free block found */
        }
    }
    /* we can write to target block */
    pthread_rwlock_wrlock(&target->rwlock);
    memcpy(target->url, url, MAXLINE);
    memcpy(target->data, data, len);
    target->datasize = len;
    target->timestamp = get_timestamp();
    pthread_rwlock_unlock(&target->rwlock);
    printf("write content into cache\n");
}

int64_t get_timestamp() {
    struct timeval time;
    gettimeofday(&time, NULL);
    int64_t s1 = (int64_t)(time.tv_sec) * 1000;
    int64_t s2 = (time.tv_usec / 1000);
    return s1 + s2;
}