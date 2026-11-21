#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "switch.h"

struct port ports[max_ports + 1];
int num_ports;
pthread_mutex_t port_lock;
struct mac_table macs;
sem_t free_ports;

static int listen_fd = -1;

static int quitting = 0;
static pthread_mutex_t quit_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t quit_cond = PTHREAD_COND_INITIALIZER;

static int is_quitting(void)
{
    int q;
    pthread_mutex_lock(&quit_lock);
    q = quitting;
    pthread_mutex_unlock(&quit_lock);
    return q;
}

static void send_full(int c)
{
    const char *msg = "switch full\n";
    if (write_full(c, msg, strlen(msg)) < 0)
    {
        perror("write");
    }
}

static void *accept_main(void *arg)
{
    struct timespec ts;
    int c, r, id;

    (void)arg;
    while (1)
    {
        c = accept(listen_fd, NULL, NULL);
        if (c < 0)
        {
            if (is_quitting())
            {
                break;
            }
            if (errno == EINTR || errno == ECONNABORTED)
            {
                continue;
            }
            perror("accept");
            break;
        }

        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        {
            perror("clock_gettime");
        }
        ts.tv_sec += port_wait;
        while ((r = sem_timedwait(&free_ports, &ts)) == -1 && errno == EINTR)
            ;
        if (is_quitting())
        {
            close(c);
            break;
        }
        if (r == -1)
        {
            printf("no free port after %d s, host rejected\n", port_wait);
            send_full(c);
            close(c);
            continue;
        }

        id = port_attach(c);
        if (id == -1)
        {
            send_full(c);
            close(c);
            continue;
        }
        if (id == -2)
        {
            fprintf(stderr, "could not start threads for a new host\n");
            close(c);
            if (sem_post(&free_ports) < 0)
                perror("sem_post");
            continue;
        }
        printf("port %d: host connected\n", id);
    }
    return NULL;
}

static void *aging_main(void *arg)
{
    struct timespec ts;
    int n;

    (void)arg;
    pthread_mutex_lock(&quit_lock);
    while (!quitting)
    {
        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
            perror("clock_gettime");
        ts.tv_sec += 1;
        pthread_cond_timedwait(&quit_cond, &quit_lock, &ts);
        if (quitting)
            break;
        pthread_mutex_unlock(&quit_lock);

        n = mac_table_age(&macs, aging_time);
        if (n > 0)
            printf("aging: removed %d MAC entries\n", n);

        pthread_mutex_lock(&quit_lock);
    }
    pthread_mutex_unlock(&quit_lock);
    return NULL;
}

static int open_listen(int port)
{
    struct sockaddr_in addr;
    int one = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        perror("socket");
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        perror("setsockopt");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 16) < 0)
    {
        perror("listen");
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv)
{
    pthread_t accept_thr, aging_thr;
    sigset_t sigs;
    int sig, tcp_port;

    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <tcp_port> <num_ports>\n", argv[0]);
        return 1;
    }
    tcp_port = atoi(argv[1]);
    num_ports = atoi(argv[2]);
    if (tcp_port <= 0 || tcp_port > 65535 || num_ports < 1 || num_ports > max_ports)
    {
        fprintf(stderr, "tcp_port 1..65535, num_ports 1..%d\n", max_ports);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        return 1;
    }
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &sigs, NULL) != 0)
    {
        fprintf(stderr, "pthread_sigmask failed\n");
        return 1;
    }

    mac_table_init(&macs, max_macs, max_per_port);
    ports_init();
    if (sem_init(&free_ports, 0, num_ports) < 0)
    {
        perror("sem_init");
        return 1;
    }
    listen_fd = open_listen(tcp_port);
    if (listen_fd < 0)
    {
        return 1;
    }

    if (pthread_create(&aging_thr, NULL, aging_main, NULL) != 0 ||
        pthread_create(&accept_thr, NULL, accept_main, NULL) != 0)
    {
        fprintf(stderr, "cant start threads\n");
        return 1;
    }
    printf("switch up on tcp port %d with %d ports\n", tcp_port, num_ports);

    while (sigwait(&sigs, &sig) != 0);
    printf("got signal %d, shutting down\n", sig);

    pthread_mutex_lock(&quit_lock);
    quitting = 1;
    pthread_cond_broadcast(&quit_cond);
    pthread_mutex_unlock(&quit_lock);

    if (shutdown(listen_fd, SHUT_RDWR) < 0)  
    {
        perror("shutdown listen");
    }
    if (sem_post(&free_ports) < 0)
    {           
        perror("sem_post");
    }
    pthread_join(accept_thr, NULL);
    pthread_join(aging_thr, NULL);

    ports_close_all();

    close(listen_fd);
    ports_destroy();
    mac_table_free(&macs);
    sem_destroy(&free_ports);
    printf("switch stopped\n");
    return 0;
}
