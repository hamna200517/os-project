#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "switch.h"

static void send_to(int out, struct frame *f)
{
    if (ports[out].state != port_active)
        return;
    frame_hold(f);
    if (queue_push(&ports[out].q, f) < 0)
        frame_put(f);  
}

static void forward(int in, struct frame *f)
{
    int out = -1;

    if (!mac_is_broadcast(f->h.dst))
    {
        out = mac_table_lookup(&macs, f->h.dst);
    }
    if (out == in)
    {
        return;
    }

    if (out > 0 && ports[out].state == port_active)
    {
        send_to(out, f);
        return;
    }
    for (int i = 1; i <= num_ports; i++)
    {
        if (i != in)
            send_to(i, f);
    }
}

static void close_port(struct port *p)
{
    int post;

    pthread_mutex_lock(&port_lock);
    p->state = port_closing;    
    pthread_mutex_unlock(&port_lock);

    queue_close(&p->q);          
    kick_socket(p->fd);     
    pthread_join(p->tx, NULL);
    queue_drain(&p->q);        
    if (close(p->fd) < 0)
    {
        perror("close");
    }
    mac_table_remove_port(&macs, p->id);

    pthread_mutex_lock(&port_lock);
    p->fd = -1;
    p->state = port_free;
    post = p->up;
    pthread_mutex_unlock(&port_lock);

    if (post && sem_post(&free_ports) < 0)
    {
        perror("sem_post");
    }
    printf("port %d: host disconnected (%ld frames dropped)%s\n", p->id,
           queue_dropped(&p->q), post ? "" : ", port stays down");
}

static void *rx_main(void *arg)
{
    struct port *p = arg;
    struct frame *f;

    while (frame_read(p->fd, &f) > 0)
    {
        pthread_mutex_lock(&port_lock);
        if (!mac_is_broadcast(f->h.src) &&
            mac_table_insert(&macs, f->h.src, p->id) == mac_portfull)
        {
            p->up = 0;  
            pthread_mutex_unlock(&port_lock);
            frame_put(f);
            printf("port %d: too many MACs (MAC flooding?), shutting it down\n", p->id);
            break;
        }
        forward(p->id, f);
        pthread_mutex_unlock(&port_lock);
        frame_put(f);
    }

    close_port(p);
    return NULL;
}

static void *tx_main(void *arg)
{
    struct port *p = arg;
    struct frame *f;

    while ((f = queue_pop(&p->q)) != NULL)
    {
        if (frame_send(p->fd, f) < 0)
        {
            frame_put(f);
            kick_socket(p->fd);
            break;
        }
        frame_put(f);
    }
    return NULL;
}

void ports_init(void)
{
    pthread_mutex_init(&port_lock, NULL);
    for (int i = 0; i <= max_ports; i++)
    {
        ports[i].id = i;
        ports[i].fd = -1;
        ports[i].state = port_free;
        ports[i].up = 1;
        ports[i].joined = 1;
        queue_init(&ports[i].q);
    }
}

void ports_destroy(void)
{
    for (int i = 0; i <= max_ports; i++)
        queue_destroy(&ports[i].q);
    pthread_mutex_destroy(&port_lock);
}

int port_attach(int fd)
{
    struct port *p = NULL;

    pthread_mutex_lock(&port_lock);
    for (int i = 1; i <= num_ports; i++)
    {
        if (ports[i].state == port_free && ports[i].up)
        {
            p = &ports[i];
            break;
        }
    }
    if (p == NULL)
    {
        pthread_mutex_unlock(&port_lock);
        return -1;
    }

    if (!p->joined)
    {
        pthread_join(p->rx, NULL);
        p->joined = 1;
    }

    p->fd = fd;
    queue_open(&p->q);
    if (pthread_create(&p->tx, NULL, tx_main, p) != 0)
    {
        queue_close(&p->q);
        p->fd = -1;
        pthread_mutex_unlock(&port_lock);
        return -2;
    }
    if (pthread_create(&p->rx, NULL, rx_main, p) != 0)
    {
        queue_close(&p->q);
        pthread_join(p->tx, NULL);
        p->fd = -1;
        pthread_mutex_unlock(&port_lock);
        return -2;
    }
    p->state = port_active;
    p->joined = 0;
    pthread_mutex_unlock(&port_lock);
    return p->id;
}

void ports_close_all(void)
{
    int join;

    pthread_mutex_lock(&port_lock);
    for (int i = 1; i <= num_ports; i++)
    {
        if (ports[i].state == port_active)
            kick_socket(ports[i].fd);
    }
    pthread_mutex_unlock(&port_lock);

    for (int i = 1; i <= num_ports; i++)
    {
        pthread_mutex_lock(&port_lock);
        join = !ports[i].joined;
        ports[i].joined = 1;
        pthread_mutex_unlock(&port_lock);
        if (join)
            pthread_join(ports[i].rx, NULL);
    }
}
