#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

#include "switch.h"
#include "logger.h"
#include "stats.h"


static void update_stats(struct port *p)
{
    stats_set_port(p->id, p->state == port_active, p->up, p->vlan, p->trunk);
}

static int can_send(struct port *p, int vlan)
{
    if (p->state != port_active)
    {
        return 0;
    }
    return p->trunk || p->vlan == vlan;
}

static int send_to(int out, struct frame *f)
{
    frame_hold(f);
    if (queue_push(&ports[out].q, f) < 0)
    {
        frame_put(f);
        stats_add(out, stat_drop, 1);
        return -1;
    }
    return 0;
}

static void mirror(int x, struct frame *f)
{
    int s = ports[x].mirror_to;
    if (s > 0 && ports[s].state == port_active && send_to(s, f) == 0)
    {
        stats_add(x, stat_mirror, 1);
    }
}

static void forward(int in, struct frame *f)
{
    int vlan = f->h.vlan;
    int out = -1;

    mirror(in, f);

    if (!mac_is_broadcast(f->h.dst))
    {
        out = mac_table_lookup(&macs, f->h.dst);
    }
    if (out == in)
        return;

    if (out > 0 && can_send(&ports[out], vlan))
    {
        if (send_to(out, f) == 0)
            mirror(out, f);
        return;
    }

    for (int i = 1; i <= num_ports; i++)
    {
        if (i != in && can_send(&ports[i], vlan))
        {
            if (send_to(i, f) == 0)
                mirror(i, f);
        }
    }
}

static void close_port(struct port *p)
{
    int post;

    pthread_mutex_lock(&port_lock);
    p->state = port_closing;    
    update_stats(p);
    pthread_mutex_unlock(&port_lock);

    queue_close(&p->q);         
    kick_socket(p->fd);      
    pthread_join(p->tx, NULL);
    queue_drain(&p->q);       
    if (close(p->fd) < 0)
    {
        log_msg("port %d: close: %s", p->id, strerror(errno));
    }
    mac_table_remove_port(&macs, p->id);

    pthread_mutex_lock(&port_lock);
    p->fd = -1;
    p->state = port_free;
    post = p->up;
    update_stats(p);
    pthread_mutex_unlock(&port_lock);

    if (post && sem_post(&free_ports) < 0)
    {
        perror("sem_post");
    }
    log_msg("port %d: host disconnected%s", p->id, post ? "" : ", port is down");
}

static void *rx_main(void *arg)
{
    struct port *p = arg;
    struct frame *f;

    while (frame_read(p->fd, &f) > 0)
    {
        stats_add(p->id, stat_in, 1);

        pthread_mutex_lock(&port_lock);
        if (!p->trunk)
        {
            frame_set_vlan(f, p->vlan);
        }
        else if (f->h.vlan < 1 || f->h.vlan > 4094)
        {
            pthread_mutex_unlock(&port_lock);
            frame_put(f);
            continue;
        }

        if (!mac_is_broadcast(f->h.src) && mac_table_insert(&macs, f->h.src, p->id) == mac_portfull)
        {
            p->up = 0;    
            pthread_mutex_unlock(&port_lock);
            frame_put(f);
            log_msg("port %d: too many MACs, shutting it down", p->id);
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
        stats_add(p->id, stat_out, 1);
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
        ports[i].vlan = 1;
        ports[i].trunk = 0;
        ports[i].mirror_to = 0;
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
    int rate;

    pthread_mutex_lock(&conf_lock);
    rate = conf.rate;
    pthread_mutex_unlock(&conf_lock);

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
    queue_open(&p->q, rate);
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
    update_stats(p);
    pthread_mutex_unlock(&port_lock);
    return p->id;
}

void port_shutdown(int id)
{
    struct port *p = &ports[id];
    int join;

    pthread_mutex_lock(&port_lock);
    if (p->up)
    {
        p->up = 0;
        if (p->state == port_free && sem_trywait(&free_ports) < 0 && errno != EAGAIN)
            perror("sem_trywait");
    }
    if (p->state == port_active)
        kick_socket(p->fd);         
    join = !p->joined;
    p->joined = 1;
    update_stats(p);
    pthread_mutex_unlock(&port_lock);

    if (join)
        pthread_join(p->rx, NULL);
}

void port_up(int id)
{
    int post = 0;

    pthread_mutex_lock(&port_lock);
    if (!ports[id].up)
    {
        ports[id].up = 1;
        if (ports[id].state == port_free)
        {
            post = 1;
        }
    }
    update_stats(&ports[id]);
    pthread_mutex_unlock(&port_lock);

    if (post && sem_post(&free_ports) < 0)
    {
        perror("sem_post");
    }
}

int port_set_vlan(int id, int vlan)
{
    if (id < 1 || id > num_ports || vlan < 1 || vlan > 4094)
    {
        return -1;
    }
    pthread_mutex_lock(&port_lock);
    ports[id].vlan = vlan;
    update_stats(&ports[id]);
    pthread_mutex_unlock(&port_lock);

    mac_table_remove_port(&macs, id);
    return 0;
}

int port_set_mirror(int id, int sniffer)
{
    if (id < 1 || id > num_ports || sniffer < 0 || sniffer > num_ports || sniffer == id)
        return -1;
    pthread_mutex_lock(&port_lock);
    ports[id].mirror_to = sniffer;
    pthread_mutex_unlock(&port_lock);
    return 0;
}

void ports_apply_config(struct config *c)
{
    pthread_mutex_lock(&port_lock);
    for (int i = 1; i <= num_ports; i++)
    {
        ports[i].vlan = c->vlan[i];
        ports[i].trunk = (i == c->trunk);
        update_stats(&ports[i]);
    }
    pthread_mutex_unlock(&port_lock);

    for (int i = 1; i <= num_ports; i++)
    {
        queue_set_rate(&ports[i].q, c->rate);
    }
}

void ports_print(FILE *out)
{
    const char *names[] = {"free", "host", "closing"};

    fprintf(out, "PORT  STATE    UP    VLAN  TRUNK  MIRROR\n");
    pthread_mutex_lock(&port_lock);
    for (int i = 1; i <= num_ports; i++)
    {
        struct port *p = &ports[i];
        fprintf(out, "%-5d %-8s %-5s %-5d %-6s ", i, names[p->state],
                p->up ? "up" : "down", p->vlan, p->trunk ? "yes" : "no");
        if (p->mirror_to > 0)
        {
            fprintf(out, "-> %d\n", p->mirror_to);
        }
        else
        {
            fprintf(out, "-\n");
        }
    }
    pthread_mutex_unlock(&port_lock);
}

void ports_close_all(void)
{
    int join;

    pthread_mutex_lock(&port_lock);
    for (int i = 1; i <= num_ports; i++)
    {
        if (ports[i].state == port_active)
        {
            kick_socket(ports[i].fd);
        }
    }
    pthread_mutex_unlock(&port_lock);

    for (int i = 1; i <= num_ports; i++)
    {
        pthread_mutex_lock(&port_lock);
        join = !ports[i].joined;
        ports[i].joined = 1;
        pthread_mutex_unlock(&port_lock);
        if (join)
        {
            pthread_join(ports[i].rx, NULL);
        }
    }
}
