#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "queue.h"

static int ms_since(const struct timespec *t)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
        perror("clock_gettime");
    }
    return (now.tv_sec - t->tv_sec) * 1000 + (now.tv_nsec - t->tv_nsec) / 1000000;
}

static int total(struct port_queue *q)
{
    return q->q[0].count + q->q[1].count + q->q[2].count;
}

static struct frame *ring_pop(struct ring *r)
{
    struct frame *f = r->fr[r->head];
    r->head = (r->head + 1) % queuesize;
    r->count--;
    return f;
}

static int pick_queue(struct port_queue *q)
{
    int p;
    for (p = low_priority; p >= normal_priority && !q->aged_used; p--)
    {
        struct ring *r = &q->q[p];
        if (r->count > 0 && ms_since(&r->fr[r->head]->arrived) > aging_ms)
        {
            q->aged_used = 1;
            return p;
        }
    }

    for (int round = 0; round < 2; round++)
    {
        for (p = 0; p < 3; p++)
        {
            if (q->q[p].count > 0 && q->credit[p] > 0)
                return p;
        }
        for (p = 0; p < 3; p++)
            q->credit[p] = q->weight[p];
        q->aged_used = 0;
    }
    return -1;
}

void queue_init(struct port_queue *q)
{
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->sleep, NULL);
    pthread_cond_init(&q->wait_tokens, NULL);
    for (int i = 0; i < 3; i++)
    {
        q->q[i].head = 0;
        q->q[i].tail = 0;
        q->q[i].count = 0;
    }
    q->weight[high_priority] = 5;
    q->weight[normal_priority] = 3;
    q->weight[low_priority] = 1;
    q->rate = 0;
    q->tokens = 0;
    q->close = 1;
    q->queuefull = 0;
}

void queue_destroy(struct port_queue *q)
{
    queue_drain(q);
    pthread_cond_destroy(&q->sleep);
    pthread_cond_destroy(&q->wait_tokens);
    pthread_mutex_destroy(&q->lock);
}

void queue_open(struct port_queue *q, int rate)
{
    pthread_mutex_lock(&q->lock);
    q->close = 0;
    q->rate = rate;
    q->tokens = rate;
    for (int i = 0; i < 3; i++)
    {
        q->credit[i] = q->weight[i];
    }
    q->aged_used = 0;
    pthread_mutex_unlock(&q->lock);
}

int queue_push(struct port_queue *q, struct frame *f)
{
    int p = f->h.priority;    
    struct ring *r;

    if (p > low_priority)
    {
        p = low_priority;
    }

    pthread_mutex_lock(&q->lock);
    r = &q->q[p];
    if (q->close || r->count == queuesize)
    {
        q->queuefull++;
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    r->fr[r->tail] = f;
    r->tail = (r->tail + 1) % queuesize;
    r->count++;
    pthread_cond_signal(&q->sleep);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

struct frame *queue_pop(struct port_queue *q)
{
    struct frame *f;
    struct timespec ts;
    int p;

    pthread_mutex_lock(&q->lock);
    while (1)
    {
        if (q->close)
        {
            pthread_mutex_unlock(&q->lock);
            return NULL;
        }
        if (total(q) == 0)
        {
            pthread_cond_wait(&q->sleep, &q->lock);
            continue;
        }
        // tier 2: no tokens, wait for the timer thread (at most 100 ms)
        if (q->rate > 0 && q->tokens == 0)
        {
            if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
                perror("clock_gettime");
            ts.tv_nsec += 100 * 1000000L;
            if (ts.tv_nsec >= 1000000000L)
            {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&q->wait_tokens, &q->lock, &ts);
            continue;
        }
        break;
    }

    p = pick_queue(q);
    f = ring_pop(&q->q[p]);
    if (q->credit[p] > 0)
    {
        q->credit[p]--;
    }
    if (q->rate > 0)
    {
        q->tokens--;
    }
    pthread_mutex_unlock(&q->lock);
    return f;
}

void queue_close(struct port_queue *q)
{
    pthread_mutex_lock(&q->lock);
    q->close = 1;
    pthread_cond_broadcast(&q->sleep);
    pthread_cond_broadcast(&q->wait_tokens);
    pthread_mutex_unlock(&q->lock);
}

void queue_drain(struct port_queue *q)
{
    struct frame *left[3 * queuesize];
    int n = 0;

    pthread_mutex_lock(&q->lock);
    for (int p = 0; p < 3; p++)
    {
        while (q->q[p].count > 0)
            left[n++] = ring_pop(&q->q[p]);
        q->q[p].head = 0;
        q->q[p].tail = 0;
    }
    pthread_mutex_unlock(&q->lock);

    for (int i = 0; i < n; i++)
    {
        frame_put(left[i]);
    }
}

long queue_dropped(struct port_queue *q)
{
    long d;
    pthread_mutex_lock(&q->lock);
    d = q->queuefull;
    pthread_mutex_unlock(&q->lock);
    return d;
}

void queue_set_rate(struct port_queue *q, int rate)
{
    pthread_mutex_lock(&q->lock);
    q->rate = rate;
    if (q->tokens > rate)
    {
        q->tokens = rate;
    }
    pthread_cond_broadcast(&q->wait_tokens);
    pthread_mutex_unlock(&q->lock);
}

void queue_tick(struct port_queue *q, int tick)
{
    int add;

    pthread_mutex_lock(&q->lock);
    if (q->rate > 0 && !q->close)
    {
        add = q->rate / 10;
        if (tick < q->rate % 10)
        {
            add++;
        }
        q->tokens += add;
        if (q->tokens > q->rate)
        {
            q->tokens = q->rate;
        }
        if (add > 0)
        {
            pthread_cond_signal(&q->wait_tokens);
        }
    }
    pthread_mutex_unlock(&q->lock);
}