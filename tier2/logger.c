#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

#include "logger.h"


#define log_slots 256
#define log_length 200

struct log_line
{
    time_t when;
    char text[log_length];
};

static struct log_line lines[log_slots];
static int head = 0;
static int tail = 0;
static int count = 0;
static int stop = 0;
static long lost = 0;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cond = PTHREAD_COND_INITIALIZER;
static pthread_t log_thread;
static FILE *log_file = NULL;

void log_msg(const char *fmt, ...)
{
    char text[log_length];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&log_lock);
    if (count == log_slots)
    {
        lost++;    
    }
    else
    {
        lines[tail].when = time(NULL);
        memcpy(lines[tail].text, text, log_length);
        tail = (tail + 1) % log_slots;
        count++;
        pthread_cond_signal(&log_cond);
    }
    pthread_mutex_unlock(&log_lock);
}

static void *logger_main(void *arg)
{
    struct log_line line;
    struct tm tm;
    char stamp[32];

    (void)arg;
    pthread_mutex_lock(&log_lock);
    while (1)
    {
        while (count == 0 && !stop)
        {
            pthread_cond_wait(&log_cond, &log_lock);
        }
        if (count == 0 && stop)
            break;

        line = lines[head];
        head = (head + 1) % log_slots;
        count--;
        pthread_mutex_unlock(&log_lock);

        localtime_r(&line.when, &tm);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(log_file, "[%s] %s\n", stamp, line.text);
        fflush(log_file);
        printf("[%s] %s\n", stamp, line.text);
        fflush(stdout);

        pthread_mutex_lock(&log_lock);
    }
    pthread_mutex_unlock(&log_lock);
    return NULL;
}

int logger_start(const char *path)
{
    log_file = fopen(path, "a");
    if (log_file == NULL)
    {
        perror(path);
        return -1;
    }
    if (pthread_create(&log_thread, NULL, logger_main, NULL) != 0)
    {
        fprintf(stderr, "cant start logger thread\n");
        fclose(log_file);
        log_file = NULL;
        return -1;
    }
    return 0;
}

void logger_stop(void)
{
    if (log_file == NULL)
    {
        return;
    }
    pthread_mutex_lock(&log_lock);
    stop = 1;
    pthread_cond_signal(&log_cond);
    pthread_mutex_unlock(&log_lock);
    pthread_join(log_thread, NULL);

    if (lost > 0)
    {
        fprintf(log_file, "(%ld log lines were lost, queue was full)\n", lost);
    }
    if (fclose(log_file) != 0)
    {
        perror("fclose log");
    }
    log_file = NULL;
}
