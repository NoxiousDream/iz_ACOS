#!/bin/bash
set -e

echo "========================================="
echo "  Тестирование задания 3 (вариант 11)"
echo "========================================="

echo ""
echo "=== Компиляция ==="
gcc -std=c99 -Wall -o hospital_named     hospital_named.c     -lrt -lpthread
gcc -std=c99 -Wall -o hospital_unnamed   hospital_unnamed.c   -lrt -lpthread
gcc -std=c99 -Wall -o hospital_ind_shm   hospital_ind_shm.c   -lrt -lpthread
gcc -std=c99 -Wall -o hospital_ind_pipe  hospital_ind_pipe.c  -lpthread
echo "Компиляция успешна."

echo ""
echo "=== Тест 1: Именованные семафоры + shared memory (4-5 баллов) ==="
timeout 30 ./hospital_named 10 42 || true
echo ""

echo "=== Тест 2: Безымянные семафоры в shared memory (6 баллов) ==="
timeout 30 ./hospital_unnamed 10 42 || true
echo ""

echo "=== Тест 3: Независимые процессы + shared memory (7-8 баллов) ==="
timeout 30 ./hospital_ind_shm auto 10 42 || true
echo ""

echo "=== Тест 4: Независимые процессы + каналы (9-10 баллов) ==="
timeout 30 ./hospital_ind_pipe auto 8 42 || true
echo ""

echo "=== Тест 5: Прерывание по Ctrl+C (SIGINT) ==="
echo "Запуск hospital_named с 500 пациентами, прерывание через 2 сек..."
timeout 2 ./hospital_named 500 99 || true
echo "Прервано. Проверяем корректность завершения..."
tail -3 hospital_named.log
echo ""

echo "========================================="
echo "  Все тесты завершены."
echo "========================================="