#ifndef SWITCH_H
#define SWITCH_H

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

#include "frame.h"
#include "queue.h"
#include "mac_table.h"
#include "config.h"

#define port_free 0
#define port_active 1
#define port_closing 2

struct port
{
    int id;
    int fd;
    int state;
    int up;         
    int vlan;
    int trunk;
    int mirror_to;  
    int joined;     
    pthread_t rx;
    pthread_t tx;
    struct port_queue q;
};

// switch.c
extern struct port ports[max_ports + 1];
extern int num_ports;
extern int tcp_port;
extern pthread_mutex_t port_lock;
extern struct mac_table macs;
extern struct config conf;
extern pthread_mutex_t conf_lock;
extern sem_t free_ports;
int is_quitting(void);

// port.c
void ports_init(void);
void ports_destroy(void);
int port_attach(int fd);
void port_shutdown(int id);
void port_up(int id);
int port_set_vlan(int id, int vlan);
int port_set_mirror(int id, int sniffer);
void ports_apply_config(struct config *c);
void ports_print(FILE *out);
void ports_close_all(void);

// admin.c
int admin_start(void);
void admin_stop(void);

#endif
