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
#include "logger.h"
#include "stats.h"

struct port ports[max_ports + 1];
int num_ports;
int tcp_port;
pthread_mutex_t port_lock;
struct mac_table macs;
struct config conf;
pthread_mutex_t conf_lock = PTHREAD_MUTEX_INITIALIZER;
sem_t free_ports;

static const char *conf_path = NULL;
static int listen_fd = -1;
static sigset_t sigs;


static int quitting = 0;
static pthread_mutex_t quit_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t quit_cond = PTHREAD_COND_INITIALIZER;

int is_quitting(void)
{
    int q;
    pthread_mutex_lock(&quit_lock);
    q = quitting;
    pthread_mutex_unlock(&quit_lock);
    return q;
}

static void add_ms(struct timespec *ts, long ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static void send_full(int c)
{
    const char *msg = "switch full\n";
    if (write_full(c, msg, strlen(msg)) < 0)
    {
        log_msg("switch is full");
    }
}

static void *accept_main(void *arg)
{
    struct timespec ts;
    int c, r, id, wait;

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
            log_msg("accept: %s", strerror(errno));
            break;
        }
        if (is_quitting())
        {
            close(c);
            break;
        }

        pthread_mutex_lock(&conf_lock);
        wait = conf.port_wait;
        pthread_mutex_unlock(&conf_lock);

        if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
        {
            perror("clock_gettime");
        }
        ts.tv_sec += wait;
        while ((r = sem_timedwait(&free_ports, &ts)) == -1 && errno == EINTR);
        if (is_quitting())
        {
            close(c);
            break;
        }
        if (r == -1)
        {
            if (errno == ETIMEDOUT)
            {
                log_msg("no free port after %d s, host rejected", wait);
            }
            else
            {
                log_msg("sem_timedwait: %s", strerror(errno));
            }
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
            log_msg("could not start threads for a new host");
            close(c);
            if (sem_post(&free_ports) < 0)
            {
                perror("sem_post");
            }
            continue;
        }
        log_msg("port %d: host connected", id);
    }
    return NULL;
}

static void *aging_main(void *arg)
{
    struct timespec ts;
    int aging, n;

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

        pthread_mutex_lock(&conf_lock);
        aging = conf.aging;
        pthread_mutex_unlock(&conf_lock);

        n = mac_table_age(&macs, aging);
        if (n > 0)
        {
            log_msg("aging: removed %d MAC entries", n);
        }

        pthread_mutex_lock(&quit_lock);
    }
    pthread_mutex_unlock(&quit_lock);
    return NULL;
}

static void *timer_main(void *arg)
{
    struct timespec next;
    int tick = 0;

    (void)arg;
    if (clock_gettime(CLOCK_REALTIME, &next) < 0)
    {
        perror("clock_gettime");
    }
    pthread_mutex_lock(&quit_lock);
    while (!quitting)
    {
        add_ms(&next, 100);
        pthread_cond_timedwait(&quit_cond, &quit_lock, &next);
        if (quitting)
            break;
        pthread_mutex_unlock(&quit_lock);

        for (int i = 1; i <= num_ports; i++)
        {
            queue_tick(&ports[i].q, tick);
        }
        tick = (tick + 1) % 10;

        pthread_mutex_lock(&quit_lock);
    }
    pthread_mutex_unlock(&quit_lock);
    return NULL;
}

static void reload_config(void)
{
    struct config c;
    if (conf_path == NULL)
    {
        log_msg("no config file was given nothing to reload");
        return;
    }
    config_defaults(&c);
    if (config_load(conf_path, &c, num_ports) < 0)
    {
        log_msg("%s has errors, keeping the old config", conf_path);
        return;
    }
    pthread_mutex_lock(&conf_lock);
    conf = c;
    pthread_mutex_unlock(&conf_lock);

    mac_table_set_limits(&macs, c.max_macs, c.max_perport, c.trunk);
    ports_apply_config(&c);
    log_msg("SIGHUP: reloaded %s", conf_path);
}

static void dump_macs(void)
{
    FILE *f = fopen("mac_dump.txt", "w");
    if (f == NULL)
    {
        log_msg(" cant open mac_dump.txt: %s", strerror(errno));
        return;
    }
    mac_table_print(&macs, f);
    if (fclose(f) != 0)
    {
        log_msg(" writing mac_dump failed");
    }
    log_msg("MAC table written to mac_dump.txt");
}

static void *signal_main(void *arg)
{
    int sig;

    (void)arg;
    while (1)
    {
        if (sigwait(&sigs, &sig) != 0)
            continue;
        if (sig == SIGINT || sig == SIGTERM)
        {
            log_msg("got %s, shutting down", sig == SIGINT ? "SIGINT" : "SIGTERM");
            break;
        }
        if (sig == SIGHUP)
        {
            reload_config();
        }
        if (sig == SIGUSR1)
        {
            dump_macs();
        }
    }
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

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s <tcp_port> <num_ports> [-c config_file]\n", prog);
    exit(1);
}

int main(int argc, char **argv)
{
    pthread_t accept_thr, aging_thr, timer_thr, signal_thr;

    if (argc != 3 && argc != 5)
    {
        usage(argv[0]);
    }
    tcp_port = atoi(argv[1]);
    num_ports = atoi(argv[2]);
    if (tcp_port <= 0 || tcp_port > 65535 || num_ports < 1 || num_ports > max_ports)
    {
        fprintf(stderr, "tcp_port 1..65535, num_ports 1..%d\n", max_ports);
        return 1;
    }
    if (argc == 5)
    {
        if (strcmp(argv[3], "-c") != 0)
            usage(argv[0]);
        conf_path = argv[4];
    }

    config_defaults(&conf);
    if (conf_path != NULL && config_load(conf_path, &conf, num_ports) < 0)
        return 1;

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        perror("signal");
        return 1;
    }
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGHUP);
    sigaddset(&sigs, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &sigs, NULL) != 0)
    {
        fprintf(stderr, "pthread_sigmask failed\n");
        return 1;
    }

    if (logger_start("switch.log") < 0)
        return 1;
    if (stats_create(tcp_port, num_ports) < 0)
    {
        logger_stop();
        return 1;
    }
    mac_table_init(&macs, conf.max_macs, conf.max_perport);
    mac_table_set_limits(&macs, conf.max_macs, conf.max_perport, conf.trunk);
    ports_init();
    if (sem_init(&free_ports, 0, num_ports) < 0)
    {
        perror("sem_init");
        return 1;
    }
    ports_apply_config(&conf);

    listen_fd = open_listen(tcp_port);
    if (listen_fd < 0)
        return 1;

    if (admin_start() < 0)
    {
        fprintf(stderr, "cant start helper threads\n");
        return 1;
    }
    if (pthread_create(&aging_thr, NULL, aging_main, NULL) != 0 ||
        pthread_create(&timer_thr, NULL, timer_main, NULL) != 0 ||
        pthread_create(&accept_thr, NULL, accept_main, NULL) != 0 ||
        pthread_create(&signal_thr, NULL, signal_main, NULL) != 0)
    {
        fprintf(stderr, "cant start helper threads\n");
        return 1;
    }
    log_msg("switch up on tcp port %d with %d ports", tcp_port, num_ports);

    pthread_join(signal_thr, NULL);

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
    admin_stop();
    pthread_join(aging_thr, NULL);
    pthread_join(timer_thr, NULL);
    ports_close_all();

    close(listen_fd);
    ports_destroy();
    mac_table_free(&macs);
    sem_destroy(&free_ports);
    stats_destroy();
    log_msg("switch stopped");
    logger_stop();
    return 0;
}
