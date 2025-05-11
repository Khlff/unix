#!/bin/bash

cleanup_processes() {
    echo "Очистка старых процессов sleep..."
    pkill -f "/bin/sleep 600" 2>/dev/null
    sleep 1
}

compile_program() {
    echo "Компиляция программы myinit..."
    make
    if [ $? -ne 0 ]; then
        echo "Ошибка компиляции. Прерывание тестирования."
        exit 1
    fi
}

create_test_files() {
    echo "Создание тестовых файлов и каталогов..."

    mkdir -p test_dir/input
    mkdir -p test_dir/output
    mkdir -p test_dir/scripts

    # Создаем входной файл для процессов
    echo "Входные данные для тестовых процессов" > test_dir/input/input.txt

    # Исправлено форматирование - убраны отступы
    cat > test_dir/scripts/sleep.sh << EOF
#!/bin/bash
/bin/sleep 600
EOF
    chmod +x test_dir/scripts/sleep.sh

    # Создаем конфигурационный файл с тремя процессами
    cat > config.txt << EOF
$(pwd)/test_dir/scripts/sleep.sh $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out1.txt
$(pwd)/test_dir/scripts/sleep.sh $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out2.txt
$(pwd)/test_dir/scripts/sleep.sh $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out3.txt
EOF

    # Создаем конфигурационный файл с одним процессом
    cat > config_one.txt << EOF
$(pwd)/test_dir/scripts/sleep.sh $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out_single.txt
EOF

    echo "Тестовые файлы созданы."
}

run_daemon() {
    echo "Запуск демона myinit с конфигурацией для трех процессов..."
    ./myinit $(pwd)/config.txt

    # Ждем запуск процессов
    sleep 3

    if [ -f /tmp/myinit.log ]; then
        echo "Демон запущен. Проверяем лог-файл:"
        tail -n 10 /tmp/myinit.log
    else
        echo "Демон не запущен или лог-файл не создан."
        exit 1
    fi
}

check_processes() {
    echo "Проверка запущенных процессов sleep..."
    ps_output=$(ps -ef | grep "/bin/sleep 600" | grep -v grep)
    echo "$ps_output"

    count=$(echo "$ps_output" | wc -l)
    echo "Количество процессов sleep: $count"

    if [ "$count" -eq 3 ]; then
        echo "Проверка пройдена: запущено 3 процесса sleep"
    else
        echo "Ошибка: Ожидалось 3 процесса sleep, запущено: $count"
    fi
}

kill_second_process() {
    echo "Убиваем второй процесс sleep..."

    # Получаем PIDs процессов sleep, отсортированных по времени запуска
    sleep_pids=($(ps aux | grep "/bin/sleep 600" | grep -v grep | awk '{print $2}'))

    if [ ${#sleep_pids[@]} -ge 2 ]; then
        echo "Убиваем процесс с PID: ${sleep_pids[1]}"
        kill ${sleep_pids[1]}
    else
        echo "Ошибка: не найдено достаточно процессов sleep"
        exit 1
    fi

    sleep 5
}

change_config() {
    echo "Заменяем конфигурацию на файл с одним процессом..."

    # Получаем PID демона myinit
    PID=$(pgrep myinit | head -1)

       myinit_count=$(pgrep myinit | wc -l)
       echo "Найдено процессов myinit: $myinit_count"

       if [ -z "$PID" ]; then
           echo "Не удалось найти PID процесса myinit."
           exit 1
       fi

       # Копируем конфигурацию с одним процессом
       cp config_one.txt config.txt

       # Отображаем текущее количество sleep процессов перед SIGHUP
       echo "Процессы sleep перед SIGHUP:"
       ps aux | grep "/bin/sleep 600" | grep -v grep

       # Отправляем сигнал SIGHUP
       echo "Отправка сигнала SIGHUP процессу myinit с PID $PID..."
       kill -HUP $PID

       # Даём больше времени на обработку
       sleep 5
}

check_single_process() {
    echo "Проверка, что запущен только один процесс sleep..."

    # Даем время на завершение старых процессов
    sleep 2

    # Используем точный паттерн
    ps_output=$(ps -ef | grep "/bin/sleep 600" | grep -v grep)
    echo "$ps_output"

    count=$(echo "$ps_output" | wc -l)
    echo "Количество процессов sleep: $count"

    if [ "$count" -eq 1 ]; then
        echo "Проверка пройдена: запущен 1 процесс sleep"
    else
        echo "Ошибка: Ожидался 1 процесс sleep, запущено: $count"
    fi
}

check_log() {
    echo "Проверка лог-файла для подтверждения ожидаемых действий:"
    cat /tmp/myinit.log

    echo "Ожидаемые события в логе:"
    echo "1. Запуск трех процессов sleep"
    echo "2. Завершение и перезапуск второго процесса"
    echo "3. Завершение трех процессов после SIGHUP"
    echo "4. Запуск одного процесса"
}

stop_daemon() {
    echo "Завершение демона..."

    # Получаем PID демона
    PID=$(pgrep myinit)

    if [ -z "$PID" ]; then
        echo "Не удалось найти PID процесса myinit."
    else
        echo "Отправка сигнала SIGTERM процессу с PID $PID..."
        kill $PID

        # Проверяем, завершился ли процесс
        sleep 1
        if pgrep myinit > /dev/null; then
            echo "Процесс не завершился, принудительно завершаем..."
            kill -9 $PID
        else
            echo "Демон успешно завершен."
        fi
    fi
}

run_tests() {
    cleanup_processes
    make clean
    compile_program
    create_test_files
    run_daemon
    check_processes
    kill_second_process
    check_processes
    change_config
    check_single_process
    check_log
    stop_daemon

    echo "Тестирование завершено!"
}

run_tests