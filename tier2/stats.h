#ifndef STATS_H
#define STATS_H

#include <stdio.h>
#include <pthread.h>
#include "mac_table.h"

#define stat_in 0
#define stat_out 1
#define stat_drop 2
#define stat_mirror 3

struct port_stats
{
    long in;
    long out;
    long drop;
    long mirror;
    int active;
    int up;
    int vlan;
    int trunk;
};

struct stats_shm
{
    pthread_mutex_t lock;    
    int num_ports;
    struct port_stats port[max_ports + 1];
};


int stats_create(int tcp_port, int num_ports);
void stats_destroy(void);
struct stats_shm *stats_get(void);
void stats_add(int port, int what, long n);
void stats_set_port(int port, int active, int up, int vlan, int trunk);


struct stats_shm *stats_open(int tcp_port);

void stats_print(struct stats_shm *s, FILE *out);

#endif
