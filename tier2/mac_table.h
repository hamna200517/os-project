#ifndef MAC_TABLE_H
#define MAC_TABLE_H
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#define mac_locks 16
#define max_ports 64

#define mac_same 0
#define mac_new 1
#define mac_moved 2
#define mac_portfull 3
#define mac_tablefull 4

struct entry
{
    uint8_t mac[6];
    int port;
    long last_seen;
    struct entry *next;
};

struct bucket
{
    pthread_mutex_t lock;
    struct entry *head;
};

struct mac_table
{
    struct bucket buckets[mac_locks];
    pthread_mutex_t count_lock;  
    int count;
    int trunk_port;           
    int max_entries;
    int max_perport;
    int perport[max_ports + 1];
};


void mac_table_init(struct mac_table *t, int max_entries, int max_perport);
void mac_table_free(struct mac_table *t);
void mac_table_set_limits(struct mac_table *t, int max_entries, int max_perport, int trunk_port);

int mac_table_insert(struct mac_table *t, const uint8_t *mac, int port);

int mac_table_lookup(struct mac_table *t, const uint8_t *mac);

int mac_table_age(struct mac_table *t, int seconds);
void mac_table_remove_port(struct mac_table *t, int port);
void mac_table_clear(struct mac_table *t);
void mac_table_print(struct mac_table *t, FILE *out);


#endif

