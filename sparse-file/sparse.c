#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define DEFAULT_BLOCK_SIZE 4096

bool check_for_zeros(const char *buf, const long size) {
    for (int i = 0; i < size; i++) {
        if (buf[i] != '\0') {
            return false;
        }
    }
    return true;
}

int main(const int argc, char *argv[]) {
    int opt;
    int block_size = DEFAULT_BLOCK_SIZE;

    while ((opt = getopt(argc, argv, "b:")) != -1) {
        if (opt == 'b') {
            block_size = atoi(optarg);
            if (block_size <= 0) {
                fprintf(stderr, "incorrect block size\n");
                exit(EXIT_FAILURE);
            }
            break;
        }
        fprintf(stderr, "unknown argument\n");
    }

    int in_fd;
    int out_fd;
    if (argc - optind == 1) {
        in_fd = STDIN_FILENO;
        out_fd = open(argv[optind], O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (out_fd < 0) {
            perror("error while open output file");
            exit(EXIT_FAILURE);
        }
    } else if (argc - optind == 2) {
        in_fd = open(argv[optind], O_RDONLY);
        if (in_fd < 0) {
            perror("error while open input file");
            exit(EXIT_FAILURE);
        }
        out_fd = open(argv[optind + 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (out_fd < 0) {
            perror("error while open output file");
            close(in_fd);
            exit(EXIT_FAILURE);
        }
    } else {
        fprintf(stderr, "failed to parse arguments\n");
        exit(EXIT_FAILURE);
    }

    char buffer[block_size];
    ssize_t bytes_read = 0;
    off_t current_offset = 0;
    while ((bytes_read = read(in_fd, buffer, block_size)) > 0) {
        if (check_for_zeros(buffer, bytes_read)) {
            off_t ret = lseek(out_fd, bytes_read, SEEK_CUR);
            if (ret == (off_t) -1) {
                perror("failed to lseek");
                close(in_fd);
                close(out_fd);
                exit(EXIT_FAILURE);
            }
        } else {
            ssize_t written = write(out_fd, buffer, bytes_read);
            if (written < 0) {
                perror("failed to write not zero block");
                close(in_fd);
                close(out_fd);
                exit(EXIT_FAILURE);
            }
        }
        current_offset += bytes_read;
    }
    if (bytes_read < 0) {
        perror("failed to read");
        close(in_fd);
        close(out_fd);
        exit(EXIT_FAILURE);
    }

    if (ftruncate(out_fd, current_offset) < 0) {
        perror("failed to ftruncate");
        close(in_fd);
        close(out_fd);
        exit(EXIT_FAILURE);
    }

    close(in_fd);
    close(out_fd);
    return 0;
}
