#!/bin/bash

cleanup_old_processes() {
    echo "Очистка старых процессов sleep..."
    pkill -f "/bin/sleep 600" 2>/dev/null
    sleep 1
}

compile() {
    echo "Компиляция"
    make
    if [ $? -ne 0 ]; then
        echo "Ошибка компиляции."
        exit 1
    fi
}

create_test_env() {
    echo "Создание тестового окружения"

    mkdir -p test_dir/input
    mkdir -p test_dir/output
    echo "test input data" > test_dir/input/input.txt

    # Создаем конфигурационный файл с тремя процессами
    cat > config.txt << EOF
   /bin/sleep 600 $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out1.txt
   /bin/sleep 600 $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out2.txt
   /bin/sleep 600 $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out3.txt
EOF

    # Создаем конфигурационный файл с одним процессом
    cat > config_one.txt << EOF
/bin/sleep 600 $(pwd)/test_dir/input/input.txt $(pwd)/test_dir/output/out_single.txt
EOF
}

run_daemon() {
    echo "Запуск myinit с конфигурацией для трех процессов"
    ./myinit $(pwd)/config.txt

    sleep 3

    if [ -f /tmp/myinit.log ]; then
        echo "Демон запущен. Лог-файл:"
        tail -n 10 /tmp/myinit.log
    else
        echo "Демон не запущен или лог-файл не создан."
        exit 1
    fi
}

check_processes() {
    echo "Проверка запущенных процессов sleep"
    ps_output=$(ps -ef | grep "/bin/sleep 600" | grep -v grep)
    echo "$ps_output"

    count=$(echo "$ps_output" | wc -l)

    if [ "$count" -eq 3 ]; then
        echo "Проверка пройдена: запущено 3 процесса sleep"
    else
        echo "Ошибка: Ожидалось 3 процесса sleep, запущено: $count"
    fi
}

kill_process() {
    echo "Убиваем один процесс sleep"

    sleep_pids=($(ps aux | grep "/bin/sleep 600" | grep -v grep | awk '{print $2}'))

    if [ ${#sleep_pids[@]} -ge 2 ]; then
        echo "Убиваем процесс с PID: ${sleep_pids[1]}"
        kill ${sleep_pids[1]}
    else
        echo "Ошибка: не найдено процессов sleep"
        exit 1
    fi

    sleep 3
}

change_config() {
    echo "Заменяем на конфиг с одним процессом"

    PID=$(pgrep myinit | head -1)

    cp config_one.txt config.txt

    echo "Процессы sleep перед SIGHUP:"
    ps aux | grep "/bin/sleep 600" | grep -v grep

    echo "Отправка сигнала SIGHUP процессу myinit с PID $PID..."
    kill -HUP $PID

    sleep 5
}

check_single_process() {
    echo "Проверка, что запущен 1 процесс sleep"

    sleep 2

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
    echo "Проверка лог-файла:"
    cat /tmp/myinit.log
}

stop_daemon() {
    echo "Завершение демона"

    PID=$(pgrep myinit)

    if [ -z "$PID" ]; then
        echo "Не удалось найти PID процесса myinit."
    else
        echo "Отправка сигнала SIGTERM процессу с PID $PID"
        kill $PID

        sleep 1
        if pgrep myinit > /dev/null; then
            echo "Процесс не завершился, принудительно завершаем"
            kill -9 $PID
        else
            echo "Демон успешно завершен"
        fi
    fi
}

run_tests() {
    cleanup_old_processes
    make clean
    compile
    create_test_env
    run_daemon
    check_processes
    kill_process
    check_processes
    change_config
    check_single_process
    check_log
    stop_daemon

    echo "Тестирование завершено."
}

run_tests