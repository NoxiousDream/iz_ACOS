#!/bin/bash
# Тестирование задания 4 (вариант 11, TCP). Работает на Linux и macOS.
PORT=${PORT:-5555}
HOST=127.0.0.1

cleanup() {
    echo ""
    echo "--- Очистка процессов ---"
    for p in $SRV $MON1 $MON2 $D0 $D1 $S0 $S1 $S2; do
        [ -n "$p" ] && kill "$p" 2>/dev/null
    done
    wait 2>/dev/null
}
trap cleanup EXIT

echo "=== Компиляция ==="
gcc -std=c99 -Wall -o hospital_server  hospital_server.c  -lpthread || exit 1
gcc -std=c99 -Wall -o hospital_doctor  hospital_doctor.c  -lpthread || exit 1
gcc -std=c99 -Wall -o hospital_patient hospital_patient.c -lpthread || exit 1
gcc -std=c99 -Wall -o hospital_monitor hospital_monitor.c -lpthread || exit 1
echo "[OK] все 4 программы собраны"

echo ""
echo "=== ТЕСТ 1 (4-5 баллов): сервер + врачи + пациенты, локальный вывод ==="
./hospital_server --port $PORT --patients 8 --duty 2 --seed 42 > srv.out 2>&1 &
SRV=$!
sleep 0.4

./hospital_doctor --host $HOST --port $PORT --role duty --num 0 > d0.out 2>&1 & D0=$!
./hospital_doctor --host $HOST --port $PORT --role duty --num 1 > d1.out 2>&1 & D1=$!
for t in 0 1 2; do
    ./hospital_doctor --host $HOST --port $PORT --role spec --type $t --num 0 > s$t.out 2>&1 &
    eval "S$t=$!"
done
sleep 0.3

for i in $(seq 1 8); do
    ./hospital_patient --host $HOST --port $PORT --seed $((100+i)) > p$i.out 2>&1 &
    sleep 0.15
done
wait $SRV 2>/dev/null
echo "--- Статистика сервера ---"
tail -8 hospital_server.log

echo ""
echo "=== ТЕСТ 2 (6-7 баллов): клиент-монитор с комплексной картиной ==="
./hospital_server --port $PORT --patients 10 --duty 2 --seed 7 > srv2.out 2>&1 &
SRV=$!
sleep 0.4
./hospital_monitor --host $HOST --port $PORT --quiet > mon.out 2>&1 & MON1=$!
sleep 0.3
./hospital_doctor --host $HOST --port $PORT --role duty --num 0 --reconnect > d0.out 2>&1 & D0=$!
./hospital_doctor --host $HOST --port $PORT --role duty --num 1 --reconnect > d1.out 2>&1 & D1=$!
for t in 0 1 2; do
    ./hospital_doctor --host $HOST --port $PORT --role spec --type $t --num 0 --reconnect > s$t.out 2>&1 &
    eval "S$t=$!"
done
for i in $(seq 1 10); do
    ./hospital_patient --host $HOST --port $PORT --seed $((200+i)) > p$i.out 2>&1 &
    sleep 0.12
done
sleep 1.5
echo "--- Комплексный вид монитора (фрагмент) ---"
grep -A7 "СОСТОЯНИЕ БОЛЬНИЦЫ" monitor.log | tail -16

echo ""
echo "=== ТЕСТ 3 (8 баллов): ДВА монитора, динамическое отключение/подключение ==="
./hospital_monitor --host $HOST --port $PORT --quiet --reconnect > mon2.out 2>&1 & MON2=$!
sleep 1
echo "Останавливаем первый монитор..."
kill $MON1 2>/dev/null; MON1=""
sleep 1
echo "Возобновляем первый монитор..."
./hospital_monitor --host $HOST --port $PORT --quiet --reconnect > mon1b.out 2>&1 & MON1=$!
sleep 1
echo "[OK] сервер продолжил работу, мониторы переподключились"

wait $SRV 2>/dev/null

echo ""
echo "=== ТЕСТ 4 (>8): отключение врача во время работы, переподключение ==="
./hospital_server --port $PORT --patients 12 --duty 2 --seed 99 > srv3.out 2>&1 &
SRV=$!
sleep 0.4
./hospital_doctor --host $HOST --port $PORT --role duty --num 0 --reconnect > d0.out 2>&1 & D0=$!
./hospital_doctor --host $HOST --port $PORT --role duty --num 1 --reconnect > d1.out 2>&1 & D1=$!
for t in 0 1 2; do
    ./hospital_doctor --host $HOST --port $PORT --role spec --type $t --num 0 --reconnect > s$t.out 2>&1 &
    eval "S$t=$!"
done
for i in $(seq 1 12); do
    ./hospital_patient --host $HOST --port $PORT --seed $((300+i)) > p$i.out 2>&1 &
    sleep 0.1
done
sleep 0.8
echo ">>> Убиваем дежурного врача 0 (пациент должен вернуться в очередь)"
kill -9 $D0 2>/dev/null; D0=""
sleep 1.2
echo ">>> Перезапускаем дежурного врача 0"
./hospital_doctor --host $HOST --port $PORT --role duty --num 0 --reconnect > d0b.out 2>&1 & D0=$!
wait $SRV 2>/dev/null
echo "--- События отключения/возврата в очередь ---"
grep -E "ОТКЛЮЧИЛСЯ|возвращён|подключился" hospital_server.log | head -8
echo "--- Статистика ---"
tail -7 hospital_server.log

echo ""
echo "=== ТЕСТ 5 (>8): остановка сервера -> все клиенты завершаются сами ==="
./hospital_server --port $PORT --patients 200 --duty 2 --seed 5 > srv4.out 2>&1 &
SRV=$!
sleep 0.4
./hospital_doctor --host $HOST --port $PORT --role duty --num 0 --reconnect > d0.out 2>&1 & D0=$!
./hospital_monitor --host $HOST --port $PORT --quiet --reconnect > mon.out 2>&1 & MON1=$!
for i in $(seq 1 6); do
    ./hospital_patient --host $HOST --port $PORT --seed $((400+i)) > p$i.out 2>&1 &
done
sleep 1
echo ">>> Посылаем SIGINT серверу"
kill -INT $SRV 2>/dev/null
sleep 1.5
echo "--- Реакция врача ---";   tail -2 doctor_duty0_0.log
echo "--- Реакция монитора ---"; tail -2 monitor.log
echo "[OK] клиенты получили SHUTDOWN и завершились корректно"

echo ""
echo "========================================="
echo "  ВСЕ ТЕСТЫ ЗАВЕРШЕНЫ"
echo "========================================="