#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9000
#define DATAFILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t got_signal = 0;
static int sockfd = -1;
static int connfd = -1;

void handle_signal(int sig)
{
    got_signal = 1;
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

    while (!got_signal) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);

        connfd = accept(sockfd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            if (got_signal) break;
            continue;
        }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        syslog(LOG_INFO, "Accepted connection from %s", ip);

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

                FILE *f = fopen(DATAFILE, "a");
                fwrite(start, 1, pkt_size, f);
                fclose(f);

                f = fopen(DATAFILE, "r");
                size_t r;
                while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
                    send(connfd, buf, r, 0);
                fclose(f);

                start = nl + 1;
            }

            size_t leftover = packet_len - (start - packet);
            if (leftover && start != packet)
                memmove(packet, start, leftover);
            packet_len = leftover;
        }

        free(packet);
        syslog(LOG_INFO, "Closed connection from %s", ip);
        close(connfd);
        connfd = -1;
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    if (connfd != -1) close(connfd);
    close(sockfd);
    remove(DATAFILE);
    closelog();
    return 0;
}
