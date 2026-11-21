#ifndef SWITCH_H
#define SWITCH_H

#include <pthread.h>
#include <semaphore.h>

#include "frame.h"
#include "queue.h"
#include "mac_table.h"

#define aging_time 30    
#define max_macs 1024     
#define max_per_port 16    
#define port_wait 5        

#define port_free 0
#define port_active 1
#define port_closing 2

struct port
{
    int id;
    int fd;
    int state;
    int up;          
    int joined;     
    pthread_t rx;
    pthread_t tx;
    struct port_queue q;
};

// switch.c
extern struct port ports[max_ports + 1];
extern int num_ports;
extern pthread_mutex_t port_lock;
extern struct mac_table macs;
extern sem_t free_ports;

// port.c
void ports_init(void);
void ports_destroy(void);
int port_attach(int fd);
void ports_close_all(void);

#endif
