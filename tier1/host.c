#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "frame.h"

static int sock;
static uint8_t my_mac[6];

static void print_frame(struct headerformat *h, uint8_t *payload)
{
    char src[18];

    mac_to_str(h->src, src);
    printf("[from %s] ", src);
    for (int i = 0; i < h->length; i++)
    {
        char c = payload[i];
        putchar(c >= 32 && c < 127 ? c : '.');
    }
    putchar('\n');
    fflush(stdout);
}

static void *reader(void *arg)
{
    uint8_t hdr[header_size];
    uint8_t payload[max_payload];
    struct headerformat hf;

    (void)arg;
    while (1)
    {
        size_t got = 0;
        while (got < header_size)
        {
            ssize_t rr = read(sock, hdr + got, header_size - got);
            if (rr == 0)
                break;
            if (rr < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
            got += rr;
        }
        if (got < header_size)
        {
            if (got > 0)
            {
                printf("switch says: %.*s", (int)got, (char *)hdr);
            }
            else
            {
                printf("switch closed the connection\n");
            }
            fflush(stdout);
            exit(0);
        }

        header_deserialize(hdr, &hf);
        if (hf.length > max_payload)
        {
            printf("bad frame from switch (length %d)\n", hf.length);
            exit(1);
        }
        if (hf.length > 0 && read_full(sock, payload, hf.length) <= 0)
        {
            printf("switch closed the connection\n");
            exit(0);
        }
        print_frame(&hf, payload);
    }
    return NULL;
}

static int send_frame(const uint8_t *dst, int prio, const char *msg)
{
    uint8_t buf[header_size + max_payload];
    struct headerformat hf;
    size_t len = strlen(msg);

    if (len > max_payload)
        len = max_payload;
    hf.length = len;
    memcpy(hf.src, my_mac, 6);
    memcpy(hf.dst, dst, 6);
    hf.vlan = 1;
    hf.priority = prio;
    header_serialize(&hf, buf);
    memcpy(buf + header_size, msg, len);
    if (write_full(sock, buf, header_size + len) < 0)
    {
        perror("write");
        return -1;
    }
    return 0;
}

static void usage(void)
{
    printf("usage: ./host <switch_ip> <tcp_port> <mac> [-n]\n");
    printf("  -n = frozen host, never reads\n");
    printf("type: <dst mac or bcast> message\n");
    exit(1);
}

int main(int argc, char **argv)
{
    struct sockaddr_in address;
    pthread_t rd;
    char line[2048];
    int no_read = 0;

    if (argc < 4)
        usage();
    if (str_to_mac(argv[3], my_mac) < 0)
    {
        fprintf(stderr, "bad mac %s\n", argv[3]);
        return 1;
    }
    for (int i = 4; i < argc; i++)
    {
        if (strcmp(argv[i], "-n") == 0)
            no_read = 1;
        else
            usage();
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1)
    {
        fprintf(stderr, "bad ip %s\n", argv[1]);
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("connect");
        return 1;
    }

    if (!no_read && pthread_create(&rd, NULL, reader, NULL) != 0)
    {
        fprintf(stderr, "cant start reader thread\n");
        return 1;
    }

    while (fgets(line, sizeof(line), stdin))
    {
        char dst_s[32];
        uint8_t dst[6];
        int prio = normal_priority;
        int count = 1;
        int n = 0;
        char *msg;

        line[strcspn(line, "\n")] = '\0';
        if (sscanf(line, "%31s %n", dst_s, &n) < 1)
        {
            continue;
        }
        if (str_to_mac(dst_s, dst) < 0)
        {
            fprintf(stderr, "bad destination %s\n", dst_s);
            continue;
        }
        
        msg = line + n;
        while (msg[0] == '-' && (msg[1] == 'p' || msg[1] == 'c'))
        {
            if (msg[1] == 'p')
            {
                prio = atoi(msg + 2);
            }
            else
            {
                count = atoi(msg + 2);
            }
            while (*msg && *msg != ' ')
            {
                msg++;
            }
            while (*msg == ' ')
            {
                msg++;
            }
        }
        if (prio < 0 || prio > 2)
            prio = normal_priority;

        for (int i = 0; i < count; i++)
        {
            if (send_frame(dst, prio, msg) < 0)
                return 1;
        }
    }

    if (!no_read)
        pthread_join(rd, NULL);
    else
        pause();
    close(sock);
    return 0;
}
