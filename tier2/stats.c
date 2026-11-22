#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "stats.h"

static struct stats_shm *shm = NULL;
static char shm_name[64];

static void make_name(int tcp_port)
{
    snprintf(shm_name, sizeof(shm_name), "/vswitch_%d", tcp_port);
}

static void stats_lock(struct stats_shm *s)
{
    if (pthread_mutex_lock(&s->lock) == EOWNERDEAD)
    {
        pthread_mutex_consistent(&s->lock);
    }
}

int stats_create(int tcp_port, int num_ports)
{
    pthread_mutexattr_t attribute;
    int fd;

    make_name(tcp_port);
    fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0)
    {
        perror("shm_open");
        return -1;
    }
    if (ftruncate(fd, sizeof(struct stats_shm)) < 0)
    {
        perror("ftruncate");
        close(fd);
        shm_unlink(shm_name);
        return -1;
    }
    shm = mmap(NULL, sizeof(struct stats_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED)
    {
        perror("mmap");
        shm = NULL;
        shm_unlink(shm_name);
        return -1;
    }
    memset(shm, 0, sizeof(struct stats_shm));

    pthread_mutexattr_init(&attribute);
    pthread_mutexattr_setpshared(&attribute, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attribute, PTHREAD_MUTEX_NORMAL);
    if (pthread_mutex_init(&shm->lock, &attribute) != 0)
    {
        fprintf(stderr, "cant init shared mutex\n");
        pthread_mutexattr_destroy(&attribute);
        stats_destroy();
        return -1;
    }
    pthread_mutexattr_destroy(&attribute);
    shm->num_ports = num_ports;
    return 0;
}

void stats_destroy(void)
{
    if (shm == NULL)
        return;
    pthread_mutex_destroy(&shm->lock);
    if (munmap(shm, sizeof(struct stats_shm)) < 0)
    {
        perror("munmap");
    }
    shm = NULL;
    if (shm_unlink(shm_name) < 0)
    {
        perror("shm_unlink");
    }
}

struct stats_shm *stats_get(void)
{
    return shm;
}

void stats_add(int port, int what, long n)
{
    if (shm == NULL || port < 1 || port > max_ports)
        return;
    stats_lock(shm);
    switch (what)
    {
    case stat_in:
        shm->port[port].in += n;
        break;
    case stat_out:
        shm->port[port].out += n;
        break;
    case stat_drop:
        shm->port[port].drop += n;
        break;
    case stat_mirror:
        shm->port[port].mirror += n;
        break;
    }
    pthread_mutex_unlock(&shm->lock);
}

void stats_set_port(int port, int active, int up, int vlan, int trunk)
{
    if (shm == NULL || port < 1 || port > max_ports)
        return;
    stats_lock(shm);
    shm->port[port].active = active;
    shm->port[port].up = up;
    shm->port[port].vlan = vlan;
    shm->port[port].trunk = trunk;
    pthread_mutex_unlock(&shm->lock);
}

struct stats_shm *stats_open(int tcp_port)
{
    struct stats_shm *sm;
    int fd;

    make_name(tcp_port);
    fd = shm_open(shm_name, O_RDWR, 0);
    if (fd < 0)
    {
        perror("shm_open");
        return NULL;
    }
    sm = mmap(NULL, sizeof(struct stats_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (sm == MAP_FAILED)
    {
        return NULL;
    }
    return sm;
}

void stats_print(struct stats_shm *s, FILE *out)
{
    struct stats_shm copy;
    int n;

    if (s == NULL)
    {
        fprintf(out, "no stats\n");
        return;
    }
    stats_lock(s);
    memcpy(copy.port, s->port, sizeof(copy.port));
    n = s->num_ports;
    pthread_mutex_unlock(&s->lock);

    fprintf(out, "%-5s %-7s %-5s %-6s %10s %10s %10s %10s\n",
            "PORT", "STATE", "VLAN", "TRUNK", "IN", "OUT", "DROPPED", "MIRRORED");
    for (int i = 1; i <= n && i <= max_ports; i++)
    {
        struct port_stats *p = &copy.port[i];
        const char *state = !p->up ? "down" : (p->active ? "host" : "free");
        fprintf(out, "%-5d %-7s %-5d %-6s %10ld %10ld %10ld %10ld\n",
                i, state, p->vlan, p->trunk ? "yes" : "no",
                p->in, p->out, p->drop, p->mirror);
    }
}
