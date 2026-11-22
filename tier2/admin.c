#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include "switch.h"
#include "logger.h"
#include "stats.h"


static int admin_fd = -1;
static pthread_t admin_thread;
static char sock_path[108];

static int port_ok(int p)
{
    return p >= 1 && p <= num_ports;
}

static void run_command(char *line, FILE *out)
{
    char cmd[32] = "";
    char arg[32] = "";
    int a, b;

    line[strcspn(line, "\r\n")] = '\0';
    sscanf(line, "%31s %31s", cmd, arg);

    if (strcmp(cmd, "show") == 0 && strcmp(arg, "mac") == 0)
    {
        mac_table_print(&macs, out);
    }
    else if (strcmp(cmd, "show") == 0 && strcmp(arg, "ports") == 0)
    {
        ports_print(out);
    }
    else if (strcmp(cmd, "shutdown") == 0 && sscanf(line, "%*s %d", &a) == 1 && port_ok(a))
    {
        port_shutdown(a);
        log_msg("admin: port %d shut down", a);
        fprintf(out, "port %d is down\n", a);
    }
    else if (strcmp(cmd, "up") == 0 && sscanf(line, "%*s %d", &a) == 1 && port_ok(a))
    {
        port_up(a);
        log_msg("admin: port %d up", a);
        fprintf(out, "port %d is up\n", a);
    }
    else if (strcmp(cmd, "vlan") == 0 && sscanf(line, "%*s %d %d", &a, &b) == 2)
    {
        if (port_set_vlan(a, b) < 0)
        {
            fprintf(out, "bad port or vlan \n");
        }
        else
        {
            log_msg("admin: port %d moved to vlan %d", a, b);
            fprintf(out, "port %d is in vlan %d\n", a, b);
        }
    }
    else if (strcmp(cmd, "mirror") == 0 && sscanf(line, "%*s %d %d", &a, &b) == 2)
    {
        if (port_set_mirror(a, b) < 0)
            fprintf(out, "bad ports\n");
        else if (b == 0)
            fprintf(out, "port %d is not mirrored any more\n", a);
        else
            fprintf(out, "port %d is mirrored to port %d\n", a, b);
        log_msg("admin: mirror %d -> %d", a, b);
    }
    else if (strcmp(cmd, "clear") == 0 && strcmp(arg, "mac") == 0)
    {
        mac_table_clear(&macs);
        log_msg("admin: MAC table cleared");
        fprintf(out, "MAC table cleared\n");
    }
    else if (strcmp(cmd, "stats") == 0)
    {
        stats_print(stats_get(), out);
    }
    else
    {
        fprintf(out, "commands:\n"
                     "  show mac\n"
                     "  show ports\n"
                     "  shutdown <port>\n"
                     "  up <port>\n"
                     "  vlan <port> <vlan>\n"
                     "  mirror <port> <sniffer_port> \n"
                     "  clear mac\n"
                     "  stats\n");
    }
}

static void *admin_main(void *arg)
{
    char line[256];
    struct timeval tv;
    char *buf;
    size_t len;
    FILE *out;
    int c;

    (void)arg;
    while (1)
    {
        c = accept(admin_fd, NULL, NULL);
        if (c < 0)
        {
            if (is_quitting())
                break;
            if (errno == EINTR)
                continue;
            log_msg("admin accept: %s", strerror(errno));
            break;
        }

        tv.tv_sec = 2;
        tv.tv_usec = 0;
        if (setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            log_msg("admin setsockopt: %s", strerror(errno));
        }

        size_t bytesread = 0;
        while (bytesread < sizeof(line) - 1)
        {
            ssize_t rr = read(c, line + bytesread, sizeof(line) - 1 - bytesread);
            if (rr <= 0)
                break;
            bytesread += rr;
            if (memchr(line, '\n', bytesread) != NULL)
                break;
        }
        line[bytesread] = '\0';

        out = open_memstream(&buf, &len);
        if (out == NULL)
        {
            close(c);
            continue;
        }
        run_command(line, out);
        if (fclose(out) != 0)
        {
            close(c);
            continue;
        }
        if (write_full(c, buf, len) < 0)
            log_msg("admin: swctl went away before the answer");
        free(buf);
        close(c);
    }
    return NULL;
}

int admin_start(void)
{
    struct sockaddr_un address;

    snprintf(sock_path, sizeof(sock_path), "/tmp/vswitch_%d.sock", tcp_port);
    admin_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (admin_fd < 0)
    {
        perror("admin socket");
        return -1;
    }
    unlink(sock_path);   
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, sock_path, sizeof(address.sun_path) - 1);
    if (bind(admin_fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(admin_fd, 4) < 0)
    {
        perror("admin bind/listen");
        close(admin_fd);
        return -1;
    }
    if (pthread_create(&admin_thread, NULL, admin_main, NULL) != 0)
    {
        close(admin_fd);
        unlink(sock_path);
        return -1;
    }
    return 0;
}

void admin_stop(void)
{
    if (shutdown(admin_fd, SHUT_RDWR) < 0)
    {
        perror("shutdown admin");
    }
    pthread_join(admin_thread, NULL);
    close(admin_fd);
    unlink(sock_path);
}
