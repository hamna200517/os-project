#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include "queue.h"

void queue_init(struct port_queue *q)
{
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->sleep, NULL);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->close = 1;  
    q->queuefull = 0;
}

void queue_destroy(struct port_queue *q)
{
    queue_drain(q);
    pthread_cond_destroy(&q->sleep);
    pthread_mutex_destroy(&q->lock);
}

void queue_open(struct port_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->close = 0;
    pthread_mutex_unlock(&q->lock);
}

int queue_push(struct port_queue *q, struct frame *f)
{
    pthread_mutex_lock(&q->lock);
    if (q->close || q->count == queuesize)
    {
        q->queuefull++;
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    q->fr[q->tail] = f;
    q->tail = (q->tail + 1) % queuesize;
    q->count++;
    pthread_cond_signal(&q->sleep);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

struct frame *queue_pop(struct port_queue *q)
{
    struct frame *f;

    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && !q->close)
        pthread_cond_wait(&q->sleep, &q->lock);
    if (q->close)
    {
        pthread_mutex_unlock(&q->lock);
        return NULL;
    }
    f = q->fr[q->head];
    q->head = (q->head + 1) % queuesize;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return f;
}

void queue_close(struct port_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->close = 1;
    pthread_cond_broadcast(&q->sleep);
    pthread_mutex_unlock(&q->lock);
}

void queue_drain(struct port_queue *q)
{
    struct frame *left[queuesize];
    int n = 0;

    pthread_mutex_lock(&q->lock);
    while (q->count > 0)
    {
        left[n++] = q->fr[q->head];
        q->head = (q->head + 1) % queuesize;
        q->count--;
    }
    q->head = 0;
    q->tail = 0;
    pthread_mutex_unlock(&q->lock);

    for (int i = 0; i < n; i++)
        frame_put(left[i]);
}

long queue_dropped(struct port_queue *q)
{
    long d;
    pthread_mutex_lock(&q->lock);
    d = q->queuefull;
    pthread_mutex_unlock(&q->lock);
    return d;
}
