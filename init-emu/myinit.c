#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>

#define MAX_PROCESSES 100
#define MAX_CMD_LEN 1024
#define LOG_FILE "/tmp/myinit.log"

typedef struct {
    pid_t pid;
    char cmd[MAX_CMD_LEN];
    char stdin_file[MAX_CMD_LEN];
    char stdout_file[MAX_CMD_LEN];
} child_process_t;

child_process_t children[MAX_PROCESSES];
int num_children = 0;
char config_file[MAX_CMD_LEN];
int log_fd;
volatile sig_atomic_t received_sighup = 0;

void log_message(const char *message) {
    time_t now;
    char time_str[64];
    char buffer[MAX_CMD_LEN + 128];

    time(&now);
    strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S] ", localtime(&now));

    snprintf(buffer, sizeof(buffer), "%s%s\n", time_str, message);
    write(log_fd, buffer, strlen(buffer));
}

int is_absolute_path(const char *path) {
    return path[0] == '/';
}

void sighup_handler(int signo) {
    received_sighup = 1;
}

void daemonize() {
    pid_t pid, sid;
    int i;

    // Форк от родительского процесса
    pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    // Завершить родительский процесс
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Создание нового сеанса
    sid = setsid();
    if (sid < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

    // Форк для предотвращения получения управляющего терминала
    pid = fork();
    if (pid < 0) {
        perror("second fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Установка umask
    umask(0);

    // Изменение рабочего каталога
    if (chdir("/") < 0) {
        perror("chdir failed");
        exit(EXIT_FAILURE);
    }

    // Закрытие всех открытых файловых дескрипторов
    for (i = 0; i < sysconf(_SC_OPEN_MAX); i++) {
        close(i);
    }

    // Открытие лог-файла
    log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        exit(EXIT_FAILURE);
    }
}

int parse_config_line(char *line, char *cmd, char *stdin_file, char *stdout_file) {
    char *saveptr;
    char *token;
    char *tokens[MAX_CMD_LEN / 2];
    int token_count = 0;

    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n')
        line[len-1] = '\0';

    token = strtok_r(line, " \t", &saveptr);
    while (token != NULL && token_count < (MAX_CMD_LEN / 2)) {
        tokens[token_count++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (token_count < 3)
        return 0;

    if (!is_absolute_path(tokens[token_count-2]) || !is_absolute_path(tokens[token_count-1]))
        return 0;

    if (!is_absolute_path(tokens[0]))
        return 0;

    // Собираем команду с аргументами
    strcpy(cmd, tokens[0]);
    for (int i = 1; i < token_count - 2; i++) {
        strcat(cmd, " ");
        strcat(cmd, tokens[i]);
    }

    strcpy(stdin_file, tokens[token_count-2]);
    strcpy(stdout_file, tokens[token_count-1]);

    return 1;
}

pid_t start_process(const char *command, const char *stdin_file, const char *stdout_file, const char *stderr_file) {
    char log_buffer[MAX_CMD_LEN * 2];
    char cmd_copy[MAX_CMD_LEN];
    char *argv[MAX_CMD_LEN / 2];
    char *token, *saveptr;
    int argc = 0;

    strncpy(cmd_copy, command, MAX_CMD_LEN - 1);
    cmd_copy[MAX_CMD_LEN - 1] = '\0';

    token = strtok_r(cmd_copy, " \t", &saveptr);
    while (token != NULL && argc < (MAX_CMD_LEN / 2) - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL;

    if (argc == 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Incorrect command: %s", command);
        log_message(log_buffer);
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Failed to create proccess for command: %s", command);
        log_message(log_buffer);
        return -1;
    }

    if (pid == 0) {
        int fd_in = open(stdin_file, O_RDONLY);
        if (fd_in < 0) {
            perror("open stdin");
            exit(EXIT_FAILURE);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);

        int fd_out = open(stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror("open stdout");
            exit(EXIT_FAILURE);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);

        int fd_err = open(stderr_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0) {
            perror("open stderr");
            exit(EXIT_FAILURE);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);

        execvp(argv[0], argv);

        perror("execvp");
        exit(EXIT_FAILURE);
    }

    // Родительский процесс
    if (num_children < MAX_PROCESSES) {
        children[num_children].pid = pid;
        strncpy(children[num_children].cmd, command, MAX_CMD_LEN - 1);
        strncpy(children[num_children].stdin_file, stdin_file, MAX_CMD_LEN - 1);
        strncpy(children[num_children].stdout_file, stdout_file, MAX_CMD_LEN - 1);
        num_children++;

        snprintf(log_buffer, sizeof(log_buffer), "Proccess running [PID=%d]: %s", pid, command);
        log_message(log_buffer);
        return pid;
    } else {
        log_message("PROCCESS COUNT LIMIT REACHED");
        kill(pid, SIGTERM);
        return -1;
    }
}

pid_t spawn_process(int index) {
    if (index < 0 || index >= num_children) {
        char log_buffer[MAX_CMD_LEN];
        snprintf(log_buffer, sizeof(log_buffer), "Failed to spawn proccess: invalid proccess index: %d", index);
        log_message(log_buffer);
        return -1;
    }

    return start_process(
        children[index].cmd,
        children[index].stdin_file,
        children[index].stdout_file,
        children[index].stdout_file
    );
}

void reap_children() {
    int status;
    pid_t pid;
    char log_buffer[MAX_CMD_LEN * 2];

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < num_children; i++) {
            if (children[i].pid == pid) {
                if (WIFEXITED(status)) {
                    snprintf(log_buffer, sizeof(log_buffer),
                        "Proccess [PID=%d] exited with code= %d: %s",
                        pid, WEXITSTATUS(status), children[i].cmd);
                    log_message(log_buffer);
                } else if (WIFSIGNALED(status)) {
                    snprintf(log_buffer, sizeof(log_buffer),
                        "Proccess [PID=%d] closed by signal= %d: %s",
                        pid, WTERMSIG(status), children[i].cmd);
                    log_message(log_buffer);
                }

                // Не перезапускаем процесс, если получен SIGHUP
                if (!received_sighup) {
                    pid_t new_pid = spawn_process(i);
                    if (new_pid > 0) {
                        children[i].pid = new_pid;
                        snprintf(log_buffer, sizeof(log_buffer),
                            "Proccess restarted [PID=%d]: %s",
                            new_pid, children[i].cmd);
                        log_message(log_buffer);
                    } else {
                        snprintf(log_buffer, sizeof(log_buffer),
                            "Failed to restart proccess: %s",
                            children[i].cmd);
                        log_message(log_buffer);
                    }
                } else {
                    children[i].pid = 0;
                    snprintf(log_buffer, sizeof(log_buffer),
                        "Proccess has been stopped and will not start (SIGHUP): %s",
                        children[i].cmd);
                    log_message(log_buffer);
                }
                break;
            }
        }
    }
}

void read_config() {
    FILE *fp;
    char line[MAX_CMD_LEN];
    char cmd[MAX_CMD_LEN];
    char stdin_file[MAX_CMD_LEN];
    char stdout_file[MAX_CMD_LEN];
    char log_buffer[MAX_CMD_LEN * 2];

    snprintf(log_buffer, sizeof(log_buffer), "Read config file: %s", config_file);
    log_message(log_buffer);

    fp = fopen(config_file, "r");
    if (!fp) {
        snprintf(log_buffer, sizeof(log_buffer), "Failed to open config file: %s", config_file);
        log_message(log_buffer);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        if (parse_config_line(line, cmd, stdin_file, stdout_file)) {
            start_process(cmd, stdin_file, stdout_file, stdout_file);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "Inccorect string in config: %s", line);
            log_message(log_buffer);
        }
    }

    fclose(fp);
}

int main(int argc, char *argv[]) {
    strncpy(config_file, argv[1], MAX_CMD_LEN - 1);
    config_file[MAX_CMD_LEN - 1] = '\0';

    daemonize();

    log_message("Daemon myinit has been started");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighup_handler;
    sigaction(SIGHUP, &sa, NULL);

    signal(SIGCHLD, SIG_DFL);

    read_config();

    while (1) {
        if (received_sighup) {
            log_message("Received SIGHUP signal, rereading config");

            for (int i = 0; i < num_children; i++) {
                if (children[i].pid > 0) {
                    kill(children[i].pid, SIGTERM);
                }
            }

            sleep(2);

            reap_children();

            num_children = 0;

            read_config();

            received_sighup = 0;
        }

        reap_children();

        sleep(1);
    }

    return EXIT_SUCCESS;
}