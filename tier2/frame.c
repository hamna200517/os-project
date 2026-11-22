#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "frame.h"

void header_serialize(const struct headerformat *h, uint8_t *buf)
{
    uint16_t length = htons(h->length);
    uint16_t vlan = htons(h->vlan);

    memcpy(buf, &length, 2);
    memcpy(buf + 2, h->src, 6);
    memcpy(buf + 8, h->dst, 6);
    memcpy(buf + 14, &vlan, 2);
    buf[16] = h->priority;
}

void header_deserialize(const uint8_t *buf, struct headerformat *h)
{
    uint16_t length;
    uint16_t vlan;

    memcpy(&length, buf, 2);
    memcpy(h->src, buf + 2, 6);
    memcpy(h->dst, buf + 8, 6);
    memcpy(&vlan, buf + 14, 2);
    h->priority = buf[16];
    h->length = ntohs(length);
    h->vlan = ntohs(vlan);
}

ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t bytesread = 0;

    while (bytesread < n)
    {
        ssize_t rr = read(fd, (char *)buf + bytesread, n - bytesread);
        if (rr == 0)
        {
            return 0;
        }
        if (rr < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        bytesread += rr;
    }
    return bytesread;
}

ssize_t write_full(int fd, const void *buf, size_t n)
{
    size_t byteswritten = 0;

    while (byteswritten < n)
    {
        ssize_t rr = write(fd, (const char *)buf + byteswritten, n - byteswritten);
        if (rr < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        byteswritten += rr;
    }
    return byteswritten;
}

struct frame *frame_new(const struct headerformat *h, const uint8_t *payload)
{
    struct frame *f = malloc(sizeof(struct frame) + header_size + h->length);
    if (f == NULL)
    {
        return NULL;
    }

    f->h = *h;
    f->size = header_size + h->length;
    f->refcount = 1;
    if (pthread_mutex_init(&f->lock, NULL) != 0)
    {
        free(f);
        return NULL;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &f->arrived) < 0)
    {
        perror("clock_gettime");
    }
    header_serialize(h, f->data);
    if (h->length > 0 && payload != NULL)
    {
        memcpy(f->data + header_size, payload, h->length);
    }
    return f;
}

int frame_read(int fd, struct frame **out)
{
    uint8_t header[header_size];
    struct headerformat hf;
    struct frame *f;
    ssize_t rr;

    *out = NULL;
    rr = read_full(fd, header, header_size);
    if (rr <= 0)
    {
        return (int)rr;
    }
    header_deserialize(header, &hf);
    if (hf.length > max_payload)
        return -1;

    f = frame_new(&hf, NULL);
    if (f == NULL)
    {
        return -1;
    }
    if (hf.length > 0)
    {
        rr = read_full(fd, f->data + header_size, hf.length);
        if (rr <= 0)
        {
            frame_put(f);
            return (int)rr;
        }
    }
    *out = f;
    return 1;
}

int frame_send(int fd, struct frame *f)
{
    if (write_full(fd, f->data, f->size) < 0)
        return -1;
    return 0;
}

void frame_hold(struct frame *f)
{
    pthread_mutex_lock(&f->lock);
    f->refcount++;
    pthread_mutex_unlock(&f->lock);
}

void frame_put(struct frame *f)
{
    int last;

    pthread_mutex_lock(&f->lock);
    f->refcount--;
    last = (f->refcount == 0);
    pthread_mutex_unlock(&f->lock);

    if (last)
    {
        pthread_mutex_destroy(&f->lock);
        free(f);
    }
}

void frame_set_vlan(struct frame *f, uint16_t vlan)
{
    uint16_t v = htons(vlan);
    f->h.vlan = vlan;
    memcpy(f->data + 14, &v, 2);
}

void kick_socket(int fd)
{
    if (shutdown(fd, SHUT_RDWR) < 0)
    {
        perror("shutdown");
    }
}

int mac_is_broadcast(const uint8_t *mac)
{
    for (int i = 0; i < 6; i++)
    {
        if (mac[i] != 0xff)
        {
            return 0;
        }
    }
    return 1;
}

void mac_to_str(const uint8_t *mac, char *out)
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

int str_to_mac(const char *s, uint8_t *mac)
{
    if (strcmp(s, "bcast") == 0)
    {
        memset(mac, 0xff, 6);
        return 0;
    }

    int n = sscanf(s, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    if (n != 6)
    {
        return -1;
    }
    return 0;
}

