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

// Структура для хранения информации о дочернем процессе
typedef struct {
    pid_t pid;
    char cmd[MAX_CMD_LEN];
    char stdin_file[MAX_CMD_LEN];
    char stdout_file[MAX_CMD_LEN];
} child_process_t;

// Глобальные переменные
child_process_t children[MAX_PROCESSES];
int num_children = 0;
char config_file[MAX_CMD_LEN];
int log_fd; // Файловый дескриптор для лог-файла
volatile sig_atomic_t received_sighup = 0;

// Функция для записи в лог
void log_message(const char *message) {
    time_t now;
    char time_str[64];
    char buffer[MAX_CMD_LEN + 128];

    time(&now);
    strftime(time_str, sizeof(time_str), "[%Y-%m-%d %H:%M:%S] ", localtime(&now));

    snprintf(buffer, sizeof(buffer), "%s%s\n", time_str, message);
    write(log_fd, buffer, strlen(buffer));
}

// Функция для проверки, является ли путь абсолютным
int is_absolute_path(const char *path) {
    return path[0] == '/';
}

// Обработчик сигнала SIGHUP
void sighup_handler(int signo) {
    received_sighup = 1;
}

// Функция для демонизации процесса
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

// Разбор строки конфигурационного файла
int parse_config_line(char *line, char *cmd, char *stdin_file, char *stdout_file) {
    char *saveptr;
    char *token;

    // Удаляем символ новой строки
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n')
        line[len-1] = '\0';

    // Первый токен - команда с аргументами
    token = strtok_r(line, " \t", &saveptr);
    if (!token || !is_absolute_path(token))
        return 0;

    strcpy(cmd, token);

    // Второй токен - stdin файл
    token = strtok_r(NULL, " \t", &saveptr);
    if (!token || !is_absolute_path(token))
        return 0;

    strcpy(stdin_file, token);

    // Третий токен - stdout файл
    token = strtok_r(NULL, " \t", &saveptr);
    if (!token || !is_absolute_path(token))
        return 0;

    strcpy(stdout_file, token);

    return 1;
}

// Функция запуска процесса с перенаправлением stdin, stdout и stderr
int start_process(const char *command, const char *stdin_file, const char *stdout_file, const char *stderr_file) {
    char log_buffer[MAX_CMD_LEN * 2];
    char cmd_copy[MAX_CMD_LEN];
    char *argv[MAX_CMD_LEN / 2];
    char *token, *saveptr;
    int argc = 0;

    // Копируем строку, так как strtok_r изменяет её
    strncpy(cmd_copy, command, MAX_CMD_LEN - 1);
    cmd_copy[MAX_CMD_LEN - 1] = '\0';

    // Разбиваем команду на аргументы
    token = strtok_r(cmd_copy, " \t", &saveptr);
    while (token != NULL && argc < (MAX_CMD_LEN / 2) - 1) {
        argv[argc++] = token;
        token = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL;

    if (argc == 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Некорректная команда: %s", command);
        log_message(log_buffer);
        return -1;
    }

    // Создаем дочерний процесс
    pid_t pid = fork();

    if (pid < 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Ошибка при создании процесса для команды: %s", command);
        log_message(log_buffer);
        return -1;
    }

    if (pid == 0) {
        // Дочерний процесс

        // Перенаправляем stdin
        int fd_in = open(stdin_file, O_RDONLY);
        if (fd_in < 0) {
            perror("open stdin");
            exit(EXIT_FAILURE);
        }
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);

        // Перенаправляем stdout
        int fd_out = open(stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
            perror("open stdout");
            exit(EXIT_FAILURE);
        }
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);

        // Перенаправляем stderr
        int fd_err = open(stderr_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_err < 0) {
            perror("open stderr");
            exit(EXIT_FAILURE);
        }
        dup2(fd_err, STDERR_FILENO);
        close(fd_err);

        // Выполняем команду
        execvp(argv[0], argv);

        // Если execvp вернулся, значит произошла ошибка
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

        snprintf(log_buffer, sizeof(log_buffer), "Запущен процесс [PID=%d]: %s", pid, command);
        log_message(log_buffer);
        return 0;
    } else {
        log_message("Достигнуто максимальное количество процессов");
        kill(pid, SIGTERM);
        return -1;
    }
}

void reap_children() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < num_children; i++) {
            if (children[i].pid == pid) {
                // Логируем завершение процесса
                if (WIFEXITED(status)) {
                    log_message("Процесс %d завершился с кодом %d: %s",
                                pid, WEXITSTATUS(status), children[i].cmd);
                } else if (WIFSIGNALED(status)) {
                    log_message("Процесс %d завершен сигналом %d: %s",
                                pid, WTERMSIG(status), children[i].cmd);
                }

                // Важно! Не перезапускаем процесс, если получен SIGHUP
                if (!received_sighup) {
                    // Запускаем процесс заново только если он не был остановлен
                    // в результате обработки SIGHUP
                    pid_t new_pid = spawn_process(i);
                    if (new_pid > 0) {
                        children[i].pid = new_pid;
                        log_message("Процесс перезапущен [PID=%d]: %s",
                                   new_pid, children[i].cmd);
                    } else {
                        log_message("Ошибка при перезапуске процесса: %s",
                                   children[i].cmd);
                    }
                } else {
                    // Если получен SIGHUP, отмечаем процесс как остановленный
                    children[i].pid = 0;
                    log_message("Процесс остановлен и не будет перезапущен (SIGHUP): %s",
                               children[i].cmd);
                }
                break;
            }
        }
    }
}

// Функция чтения и применения конфигурационного файла
void read_config() {
    FILE *fp;
    char line[MAX_CMD_LEN];
    char cmd[MAX_CMD_LEN];
    char stdin_file[MAX_CMD_LEN];
    char stdout_file[MAX_CMD_LEN];
    char log_buffer[MAX_CMD_LEN * 2];

    snprintf(log_buffer, sizeof(log_buffer), "Чтение конфигурационного файла: %s", config_file);
    log_message(log_buffer);

    fp = fopen(config_file, "r");
    if (!fp) {
        snprintf(log_buffer, sizeof(log_buffer), "Ошибка открытия конфигурационного файла: %s", config_file);
        log_message(log_buffer);
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') {
            continue; // Пропускаем комментарии и пустые строки
        }

        if (parse_config_line(line, cmd, stdin_file, stdout_file)) {
            start_process(cmd, stdin_file, stdout_file, stdout_file);
        } else {
            snprintf(log_buffer, sizeof(log_buffer), "Некорректная строка в конфигурационном файле: %s", line);
            log_message(log_buffer);
        }
    }

    fclose(fp);
}

int main(int argc, char *argv[]) {
    // Проверяем аргументы командной строки
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <config_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Копируем путь к конфигурационному файлу
    strncpy(config_file, argv[1], MAX_CMD_LEN - 1);

    // Демонизация
    daemonize();

    log_message("Демон myinit запущен");

    // Устанавливаем обработчик сигнала SIGHUP
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighup_handler;
    sigaction(SIGHUP, &sa, NULL);

    // Первоначальное чтение конфигурации
    read_config();

    // Основной цикл демона
    while (1) {
        // Обрабатываем перезагрузку конфигурации при получении SIGHUP
        if (received_sighup) {
            log_message("Получен сигнал SIGHUP, перечитываем конфигурацию");
            received_sighup = 0;

            // Завершаем все дочерние процессы
            for (int i = 0; i < num_children; i++) {
                kill(children[i].pid, SIGTERM);
            }

            // Ждем некоторое время, чтобы процессы завершились
            sleep(2);

            // Проверяем завершение процессов
            reap_children();

            // Сбрасываем счетчик процессов
            num_children = 0;

            // Перечитываем конфигурацию
            read_config();
        }

        // Обрабатываем завершение дочерних процессов
        reap_children();

        // Спим, чтобы не нагружать систему
        sleep(1);
    }

    return EXIT_SUCCESS;
}