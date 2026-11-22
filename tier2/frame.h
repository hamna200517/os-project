#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>

#define header_size 17
#define max_payload 1500

#define high_priority 0
#define normal_priority 1
#define low_priority 2


struct headerformat {
    uint16_t length;
    uint8_t src[6];
    uint8_t dst[6];
    uint16_t vlan;
    uint8_t priority;
};

struct frame {
    struct headerformat h;
    int refcount;
    pthread_mutex_t lock;
    struct timespec arrived;
    int size;
    uint8_t data[];
};

void header_serialize(const struct headerformat *h, uint8_t *buf);
void header_deserialize(const uint8_t *buf, struct headerformat *h);

ssize_t read_full(int fd, void *buf, size_t n);
ssize_t write_full(int fd, const void *buf, size_t n);

struct frame *frame_new(const struct headerformat *h, const uint8_t *payload);
int frame_read(int fd, struct frame **out);
void frame_hold(struct frame *f);
void frame_put(struct frame *f);
void frame_set_vlan(struct frame *f, uint16_t vlan);

void kick_socket(int fd);

int mac_is_broadcast(const uint8_t *mac);
void mac_to_str(const uint8_t *mac, char *out);
int str_to_mac(const char *s, uint8_t *mac);

#endif
