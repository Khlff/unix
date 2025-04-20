#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#define LOCK_WAIT_INTERVAL 100000

char *filename = NULL;
char *lock_filename = NULL;
char *stats_filename = "lock_stats.txt";
unsigned long lock_count = 0;
pid_t pid;
bool running = true;

void cleanup() {
    if (lock_filename) {
        free(lock_filename);
    }
}

void sigint_handler(int sig_num) {
    running = false;

    FILE *stats_file = fopen(stats_filename, "a");
    if (stats_file) {
        fprintf(stats_file, "PID %d: %lu успешных блокировок\n", pid, lock_count);
        fclose(stats_file);
    } else {
        fprintf(stderr, "Failed to write statistics %s: %s\n",
                stats_filename, strerror(errno));
    }

    remove(lock_filename);

    cleanup();
    exit(0);
}

char *create_lock_filename(const char *fname) {
    size_t len = strlen(fname);
    char *lock_name = malloc(len + 5);
    if (!lock_name) {
        fprintf(stderr, "Failed to malloc buff\n");
        exit(1);
    }
    strcpy(lock_name, fname);
    strcat(lock_name, ".lck");
    return lock_name;
}

bool lock_file() {
    while (true) {
        int fd = open(lock_filename, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            char pid_str[20];
            sprintf(pid_str, "%d", pid);
            write(fd, pid_str, strlen(pid_str));
            close(fd);
            return true;
        }

        if (errno == EEXIST) {
            usleep(5);
        } else {
            fprintf(stderr, "Failed to create lck file %s: %s\n",
                    lock_filename, strerror(errno));
            return false;
        }
    }
}

bool unlock_file() {
    FILE *lock_file = fopen(lock_filename, "r");
    if (!lock_file) {
        fprintf(stderr, "Failed to open file %s: %s\n",
                lock_filename, strerror(errno));
        return false;
    }

    pid_t file_pid;
    if (fscanf(lock_file, "%d", &file_pid) != 1 || file_pid != pid) {
        fprintf(stderr, "Lock file %s contains someone else`s PID or incorrect\n",
                lock_filename);
        fclose(lock_file);
        return false;
    }
    fclose(lock_file);

    if (remove(lock_filename) != 0) {
        fprintf(stderr, "Failed to delete lck file %s: %s\n",
                lock_filename, strerror(errno));
        return false;
    }

    return true;
}

int main(int argc, char *argv[]) {
    pid = getpid();
    filename = argv[1];
    lock_filename = create_lock_filename(filename);

    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fprintf(stderr, "Failed to create signal handler: %s\n", strerror(errno));
        cleanup();
        return 1;
    }

    while (running) {
        if (lock_file()) {
            lock_count++;

            sleep(1);

            if (!unlock_file()) {
                fprintf(stderr, "Failed to unlock file\n");
                cleanup();
                return 1;
            }
        }
    }
    return errno;
}
