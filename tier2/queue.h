#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "frame.h"

#define queuesize 64    
#define aging_ms 200       

struct ring
{
    struct frame *fr[queuesize];
    int head;
    int tail;
    int count;
};

struct port_queue
{
    pthread_mutex_t lock;
    pthread_cond_t sleep;
    pthread_cond_t wait_tokens;
    struct ring q[3];
    int weight[3];
    int credit[3];
    int aged_used;  
    int rate;     
    int tokens;
    int close;
    long queuefull;
};

void queue_init(struct port_queue *q);
void queue_destroy(struct port_queue *q);
void queue_open(struct port_queue *q, int rate);
int queue_push(struct port_queue *q, struct frame *f);
struct frame *queue_pop(struct port_queue *q);
void queue_close(struct port_queue *q);
void queue_drain(struct port_queue *q);
long queue_dropped(struct port_queue *q);
void queue_set_rate(struct port_queue *q, int rate);
void queue_tick(struct port_queue *q, int tick);

#endif
