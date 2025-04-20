#!/bin/bash

make clean

echo "Build"
make
if [ $? -ne 0 ]; then
    echo "Ошибка компиляции"
    exit 1
fi

touch test_file

for i in {1..10}; do
    ./locker test_file &
    pids[$i]=$!
done

echo "Запущено 10 процессов блокировки тестового файла"
echo "PID: ${pids[*]}"

sleep 60

for pid in ${pids[*]}; do
    kill -SIGINT $pid
done

echo "Отправлено SIGINT, ожидание завершения"
sleep 5

echo "Статистика блокировок:"
cat lock_stats.txt

echo "Анализ статистики..."
lines=$(wc -l < lock_stats.txt)
if [ $lines -eq 10 ]; then
    echo "OK: Все 10 процессов сохранили статистику"
else
    echo "ОШИБКА: Ожидалось 10 строк в файле статистики, получено $lines"
fi

min_locks=$(grep -o "[0-9]\+ успешных блокировок" lock_stats.txt | sort -n | head -1 | grep -o "[0-9]\+")
max_locks=$(grep -o "[0-9]\+ успешных блокировок" lock_stats.txt | sort -n | tail -1 | grep -o "[0-9]\+")

echo "Минимум блокировок: $min_locks"
echo "Максимум блокировок: $max_locks"
