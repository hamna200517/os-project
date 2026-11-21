#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "frame.h"

#define queuesize 64

struct port_queue
{
    pthread_mutex_t lock;
    pthread_cond_t sleep;
    struct frame *fr[queuesize];
    int head;
    int tail;
    int count;
    int close;
    long queuefull;
};

void queue_init(struct port_queue *q);
void queue_destroy(struct port_queue *q);
void queue_open(struct port_queue *q);
int queue_push(struct port_queue *q, struct frame *f);
struct frame *queue_pop(struct port_queue *q);
void queue_close(struct port_queue *q);
void queue_drain(struct port_queue *q);
long queue_dropped(struct port_queue *q);

#endif
