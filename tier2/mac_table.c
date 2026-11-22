#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#include "mac_table.h"
#include "frame.h"
#include "logger.h"
static long now_sec(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
    {
        perror("clock_gettime");
    }
    return t.tv_sec;
}

static int hash_mac(const uint8_t *mac)
{
    unsigned int h = 0;
    for (int i = 0; i < 6; i++)
    {
        h = h * 31 + mac[i];
    }
    return h % mac_locks;
}

static struct entry *find(struct bucket *b, const uint8_t *mac)
{
    struct entry *temp = b->head;
    while (temp)
    {
        if (memcmp(temp->mac, mac, 6) == 0)
        {
            return temp;
        }
        temp = temp->next;
    }
    return NULL;
}

static void remove_entry(struct mac_table *t, struct entry **pp)
{
    struct entry *temp = *pp;
    *pp = temp->next;

    pthread_mutex_lock(&t->count_lock);
    t->count--;
    t->perport[temp->port]--;
    pthread_mutex_unlock(&t->count_lock);
    free(temp);
}

void mac_table_init(struct mac_table *t, int max_entries, int max_perport)
{
    for (int i = 0; i < mac_locks; i++)
    {
        pthread_mutex_init(&t->buckets[i].lock, NULL);
        t->buckets[i].head = NULL;
    }
    pthread_mutex_init(&t->count_lock, NULL);
    t->count = 0;
    t->trunk_port = 0;
    t->max_entries = max_entries;
    t->max_perport = max_perport;
    for (int i = 0; i <= max_ports; i++)
    {
        t->perport[i] = 0;
    }
}

void mac_table_free(struct mac_table *t)
{
    for (int i = 0; i < mac_locks; i++)
    {
        pthread_mutex_lock(&t->buckets[i].lock);
        struct entry *temp = t->buckets[i].head;
        while (temp)
        {
            struct entry *next = temp->next;
            free(temp);
            temp = next;
        }
        t->buckets[i].head = NULL;
        pthread_mutex_unlock(&t->buckets[i].lock);
        pthread_mutex_destroy(&t->buckets[i].lock);
    }
    pthread_mutex_destroy(&t->count_lock);
}

void mac_table_set_limits(struct mac_table *t, int max_entries, int max_per_port, int trunk_port)
{
    pthread_mutex_lock(&t->count_lock);
    t->max_entries = max_entries;
    t->max_perport = max_per_port;
    t->trunk_port = trunk_port;
    pthread_mutex_unlock(&t->count_lock);
}

static void evict_oldest(struct mac_table *t)
{
    uint8_t mac[6];
    long oldest = 0;
    int found = -1;

    for (int i = 0; i < mac_locks; i++)
    {
        pthread_mutex_lock(&t->buckets[i].lock);
        for (struct entry *e = t->buckets[i].head; e; e = e->next)
        {
            if (found < 0 || e->last_seen < oldest)
            {
                oldest = e->last_seen;
                memcpy(mac, e->mac, 6);
                found = i;
            }
        }
        pthread_mutex_unlock(&t->buckets[i].lock);
    }
    if (found < 0)
        return;

    struct bucket *b = &t->buckets[found];
    pthread_mutex_lock(&b->lock);
    for (struct entry **pp = &b->head; *pp; pp = &(*pp)->next)
    {
        if (memcmp((*pp)->mac, mac, 6) == 0)
        {
            remove_entry(t, pp);
            break;
        }
    }
    pthread_mutex_unlock(&b->lock);
}

static int port_is_full(struct mac_table *t, int port)
{
    return port != t->trunk_port && t->perport[port] >= t->max_perport;
}


int mac_table_insert(struct mac_table *t, const uint8_t *mac, int port)
{
    struct bucket *b = &t->buckets[hash_mac(mac)];
    struct entry *e;
    int old, full;
    char s[18];

    if (port < 1 || port > max_ports)
    {
        return mac_same;
    }
    pthread_mutex_lock(&b->lock);
    e = find(b, mac);
    if (e != NULL)
    {
        e->last_seen = now_sec();
        if (e->port == port)
        {
            pthread_mutex_unlock(&b->lock);
            return mac_same;
        }

        pthread_mutex_lock(&t->count_lock);
        if (port_is_full(t, port))
        {
            pthread_mutex_unlock(&t->count_lock);
            pthread_mutex_unlock(&b->lock);
            return mac_portfull;
        }
        t->perport[e->port]--;
        t->perport[port]++;
        pthread_mutex_unlock(&t->count_lock);

        old = e->port;
        e->port = port;
        pthread_mutex_unlock(&b->lock);

        mac_to_str(mac, s);
        printf("MAC moved: port %d -> port %d (%s)\n", old, port, s);
        return mac_moved;
    }
    pthread_mutex_unlock(&b->lock);


    pthread_mutex_lock(&t->count_lock);
    full = (t->count >= t->max_entries);
    pthread_mutex_unlock(&t->count_lock);
    if (full)
        evict_oldest(t);

    e = malloc(sizeof(struct entry));
    if (e == NULL)
        return mac_tablefull;
    memcpy(e->mac, mac, 6);
    e->port = port;
    e->last_seen = now_sec();

    pthread_mutex_lock(&b->lock);
    if (find(b, mac) != NULL)
    {
        pthread_mutex_unlock(&b->lock);
        free(e);
        return mac_same;
    }
    pthread_mutex_lock(&t->count_lock);
    if (port_is_full(t, port))
    {
        pthread_mutex_unlock(&t->count_lock);
        pthread_mutex_unlock(&b->lock);
        free(e);
        return mac_portfull;
    }
    if (t->count >= t->max_entries)
    {
        pthread_mutex_unlock(&t->count_lock);
        pthread_mutex_unlock(&b->lock);
        free(e);
        return mac_tablefull;
    }
    t->count++;
    t->perport[port]++;
    pthread_mutex_unlock(&t->count_lock);

    e->next = b->head;
    b->head = e;
    pthread_mutex_unlock(&b->lock);
    return mac_new;
}

int mac_table_lookup(struct mac_table *t, const uint8_t *mac)
{
    struct bucket *b = &t->buckets[hash_mac(mac)];
    struct entry *e;
    int port = -1;

    pthread_mutex_lock(&b->lock);
    e = find(b, mac);
    if (e != NULL)
        port = e->port;
    pthread_mutex_unlock(&b->lock);
    return port;
}

int mac_table_age(struct mac_table *t, int seconds)
{
    long now = now_sec();
    int removed = 0;

    for (int i = 0; i < mac_locks; i++)
    {
        struct bucket *b = &t->buckets[i];
        pthread_mutex_lock(&b->lock);
        struct entry **pp = &b->head;
        while (*pp)
        {
            if (now - (*pp)->last_seen > seconds)
            {
                remove_entry(t, pp);
                removed++;
            }
            else
                pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&b->lock);
    }
    return removed;
}

static void remove_where(struct mac_table *t, int port)
{
    for (int i = 0; i < mac_locks; i++)
    {
        struct bucket *b = &t->buckets[i];
        pthread_mutex_lock(&b->lock);
        struct entry **pp = &b->head;
        while (*pp)
        {
            if (port == 0 || (*pp)->port == port)
            {
                remove_entry(t, pp);
            }
            else
                pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&b->lock);
    }
}

void mac_table_remove_port(struct mac_table *t, int port)
{
    if (port >= 1)
    {
        remove_where(t, port);
    }
}

void mac_table_clear(struct mac_table *t)
{
    remove_where(t, 0);
}

void mac_table_print(struct mac_table *t, FILE *out)
{
    long now = now_sec();
    char s[18];
    int n = 0;

    fprintf(out, "%-19s %-5s %s\n", "MAC", "PORT", "AGE");
    for (int i = 0; i < mac_locks; i++)
    {
        pthread_mutex_lock(&t->buckets[i].lock);
        for (struct entry *e = t->buckets[i].head; e; e = e->next)
        {
            mac_to_str(e->mac, s);
            fprintf(out, "%-19s %-5d %lds\n", s, e->port, now - e->last_seen);
            n++;
        }
        pthread_mutex_unlock(&t->buckets[i].lock);
    }
    fprintf(out, "%d entries\n", n);
}
