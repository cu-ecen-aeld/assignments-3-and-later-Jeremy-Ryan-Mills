#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

/* glibc's sys/queue.h lacks SLIST_FOREACH_SAFE */
#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = SLIST_FIRST((head)); \
         (var) && ((tvar) = SLIST_NEXT((var), field), 1); \
         (var) = (tvar))
#endif

#define PORT 9000
#define DATAFILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t got_signal = 0;
static int sockfd = -1;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_signal(int sig)
{
    got_signal = 1;
}

typedef struct thread_data {
    pthread_t tid;
    int connfd;
    char ip[INET_ADDRSTRLEN];
    int done;
    SLIST_ENTRY(thread_data) entries;
} thread_data_t;

SLIST_HEAD(thread_list, thread_data) thread_head = SLIST_HEAD_INITIALIZER(thread_head);

void *connection_handler(void *arg)
{
    thread_data_t *td = (thread_data_t *)arg;
    int connfd = td->connfd;

    char *packet = NULL;
    size_t packet_len = 0;
    char buf[1024];
    ssize_t n;

    while ((n = recv(connfd, buf, sizeof(buf), 0)) > 0) {
        char *tmp = realloc(packet, packet_len + n);
        if (!tmp) { syslog(LOG_ERR, "realloc failed"); break; }
        packet = tmp;
        memcpy(packet + packet_len, buf, n);
        packet_len += n;

        char *start = packet;
        char *nl;
        while ((nl = memchr(start, '\n', packet_len - (start - packet))) != NULL) {
            size_t pkt_size = (nl - start) + 1;

            pthread_mutex_lock(&file_mutex);
            FILE *f = fopen(DATAFILE, "a");
            if (f) { fwrite(start, 1, pkt_size, f); fclose(f); }
            f = fopen(DATAFILE, "r");
            if (f) {
                size_t r;
                while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
                    send(connfd, buf, r, 0);
                fclose(f);
            }
            pthread_mutex_unlock(&file_mutex);

            start = nl + 1;
        }

        size_t leftover = packet_len - (start - packet);
        if (leftover && start != packet)
            memmove(packet, start, leftover);
        packet_len = leftover;
    }

    free(packet);
    syslog(LOG_INFO, "Closed connection from %s", td->ip);
    close(connfd);
    td->done = 1;
    return NULL;
}

void *timer_handler(void *arg)
{
    (void)arg;
    struct timespec req = {1, 0};
    int elapsed = 0;
    while (!got_signal) {
        nanosleep(&req, NULL);
        if (got_signal) break;
        if (++elapsed < 10) continue;
        elapsed = 0;
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char timebuf[128];
        strftime(timebuf, sizeof(timebuf), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);
        pthread_mutex_lock(&file_mutex);
        FILE *f = fopen(DATAFILE, "a");
        if (f) { fputs(timebuf, f); fclose(f); }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { closelog(); return -1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(sockfd);
        closelog();
        return -1;
    }

    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        pid_t pid = fork();
        if (pid < 0) { close(sockfd); closelog(); return -1; }
        if (pid > 0) exit(0);
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }

    listen(sockfd, 10);

    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_handler, NULL);

    while (!got_signal) {
        /* reap completed threads */
        thread_data_t *td, *tmp_td;
        SLIST_FOREACH_SAFE(td, &thread_head, entries, tmp_td) {
            if (td->done) {
                pthread_join(td->tid, NULL);
                SLIST_REMOVE(&thread_head, td, thread_data, entries);
                free(td);
            }
        }

        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        int connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            if (got_signal) break;
            continue;
        }

        thread_data_t *new_td = malloc(sizeof(thread_data_t));
        if (!new_td) { close(connfd); continue; }
        new_td->connfd = connfd;
        new_td->done = 0;
        inet_ntop(AF_INET, &client_addr.sin_addr, new_td->ip, sizeof(new_td->ip));
        syslog(LOG_INFO, "Accepted connection from %s", new_td->ip);

        if (pthread_create(&new_td->tid, NULL, connection_handler, new_td) != 0) {
            close(connfd);
            free(new_td);
            continue;
        }
        SLIST_INSERT_HEAD(&thread_head, new_td, entries);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(sockfd);

    /* signal and join all connection threads */
    thread_data_t *td;
    while (!SLIST_EMPTY(&thread_head)) {
        td = SLIST_FIRST(&thread_head);
        if (!td->done)
            shutdown(td->connfd, SHUT_RDWR);
        pthread_join(td->tid, NULL);
        SLIST_REMOVE_HEAD(&thread_head, entries);
        free(td);
    }

    pthread_join(timer_tid, NULL);

    remove(DATAFILE);
    pthread_mutex_destroy(&file_mutex);
    closelog();
    return 0;
}
