#include <stdio.h>
#include <syslog.h>
#include <string.h>

int main(int argc, char *argv[]) {
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Usage: writer <writefile> <writestr>");
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    FILE *wfile = fopen(writefile, "w");
    if (wfile == NULL) {
        syslog(LOG_ERR, "Failed to open file %s for writing", writefile);
        closelog();
        return 1;
    }

    fwrite(writestr, sizeof(char), strlen(writestr), wfile);

    fclose(wfile);
    closelog();
    return 0;
}
