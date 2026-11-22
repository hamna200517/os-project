#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "frame.h"
#include "stats.h"

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [-p tcp_port] <command>\n", prog);
    fprintf(stderr, "  show mac | show ports | shutdown <port> | up <port>\n");
    fprintf(stderr, "  vlan <port> <vlan> | mirror <port> <sniffer_port> | clear mac\n");
    fprintf(stderr, "  stats | stats --watch\n");
    fprintf(stderr, "tcp_port is the switch's tcp port, default 9000\n");
    exit(1);
}

static int watch(int port)
{
    struct stats_shm *shm = stats_open(port);

    if (shm == NULL)
    {
        fprintf(stderr, "no shared memory for port %d, is switch running?\n", port);
        return 1;
    }
    while (1)
    {
        printf("\033[H\033[2J");
        printf("switch on tcp port %d\n\n", port);
        stats_print(shm, stdout);
        fflush(stdout);
        sleep(1);
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct sockaddr_un address;
    char line[256] = "";
    char buf[4096];
    int port = 9000;
    int first = 1;
    int fd;
    ssize_t rr;

    if (argc >= 3 && strcmp(argv[1], "-p") == 0)
    {
        port = atoi(argv[2]);
        argv += 2;
        argc -= 2;
    }
    if (argc < 2)
    {
        usage(argv[0]);
    }

    if (argc == 3 && strcmp(argv[1], "stats") == 0 && strcmp(argv[2], "--watch") == 0)
    {
        return watch(port);
    }

    for (int i = 1; i < argc; i++)
    {
        if (strlen(line) + strlen(argv[i]) + 2 >= sizeof(line))
            usage(argv[0]);
        if (!first)
            strcat(line, " ");
        strcat(line, argv[i]);
        first = 0;
    }
    strcat(line, "\n");

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "/tmp/vswitch_%d.sock", port);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror(address.sun_path);
        return 1;
    }
    if (write_full(fd, line, strlen(line)) < 0)
    {
        perror("write");
        return 1;
    }
    if (shutdown(fd, SHUT_WR) < 0)
    {
        perror("shutdown");
    }

    while ((rr = read(fd, buf, sizeof(buf))) > 0)
    {
        fwrite(buf, 1, rr, stdout);
    }
    if (rr < 0)
    {
        perror("read");
    }
    close(fd);
    return 0;
}
