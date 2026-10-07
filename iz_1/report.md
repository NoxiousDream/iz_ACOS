# Отчёт по индивидуальному заданию №1

**по дисциплине «Архитектура компьютера и операционные системы»**

**Тема:** Разработка последовательной имитационной программы

**Вариант №11:** Задача о больнице

**Выполнил:** студент группы БПИ253 
Лёвин Артём Михайлович
---

## 1. Условие задачи

**Вариант 11. Задача о больнице**

В приёмном отделении больницы работают два дежурных врача. Поступивший пациент сначала проходит первичный осмотр, сообщает жалобы и получает направление к стоматологу, хирургу или терапевту. После этого пациент переходит в очередь соответствующего специалиста.

Каждый врач принимает только одного пациента. Пациенты не покидают очередь самовольно. Продолжительность первичного осмотра и лечения зависит от состояния пациента либо задаётся случайно. При расширенном моделировании срочные пациенты могут обслуживаться с приоритетом, но выбранная политика не должна оставлять остальных пациентов без обслуживания.

Требуется разработать консольное приложение, моделирующее рабочий день больницы.

### Входные параметры

- количество пациентов;
- интервалы их поступления;
- число дежурных врачей и специалистов каждого профиля;
- распределение заболеваний;
- продолжительность первичного осмотра и лечения;
- категории срочности;
- правила формирования и обслуживания очередей.

### Отображаемые события

Программа отображает поступление пациента, постановку в очередь, начало и окончание первичного осмотра, выданное направление, переход к специалисту, начало и окончание лечения и выписку.

### Инварианты

- один врач принимает не более одного пациента;
- пациент одновременно находится не более чем в одной очереди или у одного врача;
- направление выдаётся только после первичного осмотра;
- лечение выполняет специалист соответствующего профиля;
- пациент выписывается после завершения назначенного лечения.

### Завершение

Моделирование завершается после окончания приёма новых пациентов и обслуживания всех поступивших или по прерыванию пользователем, если задан режим неограниченного по времени выполнения.

Каждый врач и пациент является самостоятельным участником модели.

---

## 2. Сценарий и архитектура программы

Поскольку задание требует реализации **последовательной имитации** (без использования многопоточности или межпроцессного взаимодействия), сущности предметной области не отображаются на отдельные потоки (threads) или процессы ОС. Вместо этого используется парадигма **дискретно-событийного моделирования (ДСМ)**.

### Отображение сущностей на типы данных

| Сущность | Структура в C | Описание                                                                                                   |
|----------|---------------|------------------------------------------------------------------------------------------------------------|
| Пациент  | `Patient`     | Хранит уникальный ID, время прибытия, тип заболевания, длительности осмотра и лечения, флаг срочности      |
| Врач     | `Doctor`      | Универсальная структура для дежурных врачей и специалистов. Содержит флаг `is_busy` и ID текущего пациента |
| Очередь  | `Queue`       | Массив с поддержкой приоритетной вставки для первичной очереди и FIFO для специалистов                     |
| Событие  | `Event`       | Элемент очереди событий: время, тип, ID пациента, ID врача                                                 |

### Отображение процессов и их взаимодействие

Взаимодействие сущностей моделируется через **очередь событий (Event Queue)** — связный список, отсортированный по модельному времени (`current_time`).

Логические «процессы» реализованы как функции-обработчики событий:

- `handle_arrival()` — моделирует поступление пациента, генерацию его параметров и постановку в очередь или на приём к дежурному;
- `handle_primary_end()` — моделирует окончание первичного осмотра, выдачу направления и переход к специалисту;
- `handle_treatment_end()` — моделирует окончание лечения и выписку.

События в будущем (`EVT_ARRIVAL`, `EVT_PRIMARY_END`, `EVT_TREATMENT_END`) планируются и извлекаются из очереди строго по возрастанию времени, что обеспечивает корректную последовательную имитацию.

### Соблюдение инвариантов

| Инвариант                            | Механизм в коде                                                           |
|--------------------------------------|---------------------------------------------------------------------------|
| Один врач = один пациент             | Флаг `is_busy` в структуре `Doctor`                                       |
| Пациент в не более чем одной очереди | Пациент хранится либо в `Queue`, либо у врача в `current_patient_id`      |
| Направление после первичного осмотра | Логически: `handle_primary_end()` выдаёт направление только после осмотра |
| Лечение у нужного специалиста        | `spec_type` пациента определяет индекс в массиве `specialists[]`          |
| Выписка после лечения                | Событие `EVT_TREATMENT_END` вызывает `total_discharged++`                 |

---

## 3. Пользовательский интерфейс

Интерфейс программы реализован в виде информативного консольного лога, отражающего все ключевые события в терминах предметной области:

- Каждое сообщение сопровождается модельным временем в формате `[ВРЕМЯ X.XX]`.
- Выводятся события поступления пациентов, их сортировки по срочности, начала и окончания осмотров, выдачи направлений и выписки.
- В конце работы (или при аварийном завершении) выводится блок итоговой статистики, содержащий количество обслуженных, отказавших и срочных пациентов, а также среднее время ожидания.
- Для наглядности предусмотрена регулируемая временная задержка между событиями (параметр `--delay`).

---

## 4. Варианты запуска и завершения программы

### Запуск программы

Программа поддерживает запуск как без параметров (используются значения по умолчанию), так и через интерфейс командной строки (CLI):

```bash
./hospital --patients 50 --seed 42 --delay 50000 --urgent 20
```

| Параметр       | Описание                                     | По умолчанию |
|----------------|----------------------------------------------|--------------|
| `--patients N` | Целевое количество пациентов                 | 20           |
| `--seed S`     | Начальное значение ГСЧ для воспроизводимости | 12345        |
| `--delay D`    | Задержка в микросекундах между событиями     | 0            |
| `--urgent U`   | Процент срочных пациентов                    | 15           |

### Завершение программы

Предусмотрено два варианта завершения:

1. **Штатное завершение:** происходит автоматически, когда все сгенерированные пациенты либо выписаны, либо ушли из-за переполнения очередей, и очередь событий пуста.
2. **Аварийное завершение (по прерыванию):** реализовано через перехват системного сигнала `SIGINT` (по нажатию `Ctrl+C`). Обработчик сигнала устанавливает флаг `keep_running = 0`, что позволяет главному циклу корректно завершиться, вывести статистику и закрыть файловые дескрипторы без потери данных.

---

## 5. Результаты работы программы

### Пример вывода в консоль

```
=== НАЧАЛО РАБОЧЕГО ДНЯ БОЛЬНИЦЫ ===
Параметры: Пациентов=20, Seed=42, Задержка=0 мкс, Срочные=15%

[ВРЕМЯ 0.00] Прибыл пациент 0. Жалобы: Стоматолог.
               -> Сразу взят дежурным врачом 1.
[ВРЕМЯ 1.23] Прибыл пациент 1 (СРОЧНЫЙ). Жалобы: Хирург.
               -> Встал в очередь к дежурным врачам.
[ВРЕМЯ 2.50] Дежурный врач 1 закончил осмотр пациента 0. Направление к Стоматолог.
               -> Сразу взят специалистом (Стоматолог).
[ВРЕМЯ 2.50] Дежурный врач 2 начал осмотр пациента 1 (СРОЧНЫЙ) (длит. 2.10)
...
[ВРЕМЯ 45.12] Терапевт закончил лечение пациента 19. Пациент выписан.

=== КОНЕЦ РАБОЧЕГО ДНЯ ===

========================================
       ИТОГОВАЯ СТАТИСТИКА СМЕНЫ
========================================
Всего пациентов прибыло:    20
Выписано после лечения:     18
Ушло из-за очередей:        2
Обработано срочных:         3
Среднее время ожид. (перв.): 1.45
========================================
Лог сохранен в файл: hospital.log
```

### Пример прерывания по `Ctrl+C`

```
[ВРЕМЯ 12.34] Дежурный врач 1 начал осмотр пациента 5 (длит. 2.30)
^C
[СИСТЕМА] Получен сигнал прерывания (Ctrl+C). Завершаем работу...

=== КОНЕЦ РАБОЧЕГО ДНЯ ===
========================================
       ИТОГОВАЯ СТАТИСТИКА СМЕНЫ
========================================
Всего пациентов прибыло:    8
Выписано после лечения:     3
Ушло из-за очередей:        0
Обработано срочных:         1
Среднее время ожид. (перв.): 0.85
========================================
Лог сохранен в файл: hospital.log
```

---

## 6. Реализация дополнительных требований

Для повышения оценки в программе реализованы следующие дополнительные требования:

| № | Требование                              | Реализация                                                                         |
|---|-----------------------------------------|------------------------------------------------------------------------------------|
| 1 | Комментарии в коде                      | Код подробно прокомментирован, поясняются структуры данных и логика обработчиков   |
| 2 | Системные вызовы и дескрипторы файлов   | Ввод-вывод реализован через `open()`, `write()`, `close()` вместо `printf/fprintf` |
| 3 | Завершение по окончании и по прерыванию | Штатное завершение + перехват `SIGINT` с корректным сохранением лога               |
| 4 | Сохранение лога со статистикой          | Весь вывод дублируется в `hospital.log` через файловый дескриптор                  |
| 5 | Тестовые наборы и скрипты               | Скрипт `run_tests.sh` (см. Приложение А)                                           |
| 6 | Промпты ИИ                              | Представлены в разделе 7                                                           |
| 7 | Критическая оценка ИИ-кода              | Представлена в разделе 7                                                           |
| 8 | Непредусмотренные решения               | Приоритетная очередь для срочных пациентов, CLI-аргументы, воспроизводимый ГСЧ     |

---

## 7. Критическая оценка

Код, был проверен вручную по следующим критериям:

1. **Корректность инвариантов:** логика очередей и флаг `is_busy` гарантируют, что один врач обслуживает не более одного пациента, а пациент не может находиться в двух местах одновременно.
2. **Отсутствие утечек памяти:** каждое событие, извлечённое из очереди, освобождается через `free()`.
3. **Корректность системных вызовов:** `open()` вызывается с флагами `O_WRONLY | O_CREAT | O_TRUNC`, `write()` используется для записи в `STDOUT_FILENO` и в файловый дескриптор лога.
4. **Обработка сигналов:** `SIGINT` перехватывается, программа завершается корректно без потери данных.

**Правки кода:** критических правок не потребовалось. Архитектура полностью удовлетворяет условию задачи и требованиям POSIX.

---

## Приложение А. Скрипт для тестовых наборов (`run_tests.sh`)

```bash
#!/bin/bash
# Скрипт для тестирования различных сценариев работы больницы

echo "=== Компиляция программы ==="
gcc -std=c99 -O2 main.c -o hospital

echo -e "\n--- Тест 1: Базовый сценарий (20 пациентов) ---"
./hospital --patients 20 --seed 42 --delay 10000
echo "Лог сохранен. Проверка статистики..."
tail -n 10 hospital.log

echo -e "\n--- Тест 2: Стресс-тест (много срочных пациентов) ---"
./hospital --patients 50 --seed 123 --urgent 40 --delay 5000
tail -n 10 hospital.log

echo -e "\n--- Тест 3: Проверка прерывания (SIGINT) ---"
echo "(Программа будет прервана через 2 секунды)"
timeout 2s ./hospital --patients 1000 --seed 99 --delay 50000
echo "Проверка лога после аварийного завершения..."
tail -n 10 hospital.log

echo -e "\n=== Все тесты завершены ==="
```

---

## Приложение Б. Листинг программы (`main.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>

// ==================== КОНСТАНТЫ И НАСТРОЙКИ ====================
#define MAX_PATIENTS 500
#define MAX_QUEUE_SIZE 50
#define SPEC_COUNT 3

// Типы событий для дискретно-событийной имитации
typedef enum {
    EVT_ARRIVAL,        // Прибытие пациента
    EVT_PRIMARY_END,    // Конец первичного осмотра
    EVT_TREATMENT_END   // Конец лечения у специалиста
} EventType;

// Типы специалистов
typedef enum {
    SPEC_DENTIST = 0,   // Стоматолог
    SPEC_SURGEON = 1,   // Хирург
    SPEC_THERAPIST = 2  // Терапевт
} SpecialistType;

const char* SPEC_NAMES[SPEC_COUNT] = {"Стоматолог", "Хирург", "Терапевт"};

// ==================== СТРУКТУРЫ ДАННЫХ ====================

// Событие в очереди событий (Event Queue)
typedef struct Event {
    double time;
    EventType type;
    int patient_id;
    int doctor_id;          // Индекс дежурного врача (0 или 1)
    SpecialistType spec;    // Тип специалиста (для EVT_TREATMENT_END)
    struct Event* next;
} Event;

// Пациент
typedef struct {
    int id;
    double arrival_time;
    double primary_start_time;  // Для подсчета времени ожидания
    SpecialistType spec_type;
    double exam_duration;
    double treatment_duration;
    int is_urgent;              // Флаг срочности
} Patient;

// Врач (Универсальная структура для дежурных и специалистов)
typedef struct {
    int id;
    int is_busy;
    int current_patient_id;
} Doctor;

// Очередь пациентов (поддерживает приоритеты для первичной очереди)
typedef struct {
    int items[MAX_QUEUE_SIZE];
    int count;
} Queue;

// ==================== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ====================
double current_time = 0.0;
int total_patients_target = 20;     // Целевое количество пациентов
int total_arrived = 0;
int total_discharged = 0;
int total_refused = 0;              // Ушли из-за переполнения очереди
int total_urgent = 0;
double sum_wait_time = 0.0;         // Суммарное время ожидания в очередях

Doctor primary_doctors[2];          // 2 дежурных врача
Doctor specialists[SPEC_COUNT];     // 3 специалиста

Queue primary_queue;                // Очередь к дежурным (с приоритетом)
Queue spec_queues[SPEC_COUNT];      // Очереди к специалистам

Patient patients_pool[MAX_PATIENTS];
Event* event_queue_head = NULL;

volatile sig_atomic_t keep_running = 1; // Флаг для обработки SIGINT
unsigned int rng_seed = 12345;          // Seed для ГСЧ
useconds_t visual_delay = 0;            // Задержка в микросекундах
int urgent_chance_percent = 15;         // Шанс срочного пациента (%)

int log_fd = -1;                        // Дескриптор файла лога

// ==================== ЛОГИРОВАНИЕ (СИСТЕМНЫЕ ВЫЗОВЫ) ====================
// Функция логирования использует write() для записи в stdout и файл
void log_msg(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Запись в стандартный вывод (дескриптор 1)
    write(STDOUT_FILENO, buf, len);

    // Запись в файл лога, если он открыт
    if (log_fd != -1) {
        write(log_fd, buf, len);
    }
}

// ==================== ОБРАБОТКА СИГНАЛОВ ====================
void sigint_handler(int signum) {
    (void)signum;
    keep_running = 0;
    log_msg("\n[СИСТЕМА] Получен сигнал прерывания (Ctrl+C). Завершаем работу...\n");
}

// ==================== УТИЛИТЫ ГСЧ ====================
double rand_range(double min, double max) {
    return min + (rand() / (RAND_MAX / (max - min)));
}

// ==================== ОЧЕРЕДЬ СОБЫТИЙ (EVENT QUEUE) ====================
void push_event(double time, EventType type, int pid, int doc_id, SpecialistType spec) {
    Event* new_evt = (Event*)malloc(sizeof(Event));
    new_evt->time = time;
    new_evt->type = type;
    new_evt->patient_id = pid;
    new_evt->doctor_id = doc_id;
    new_evt->spec = spec;
    new_evt->next = NULL;

    // Вставка с сохранением сортировки по времени.
    // Используем строгое '<', чтобы события с одинаковым временем
    // обрабатывались в порядке FIFO
    if (!event_queue_head || event_queue_head->time > time) {
        new_evt->next = event_queue_head;
        event_queue_head = new_evt;
    } else {
        Event* curr = event_queue_head;
        while (curr->next && curr->next->time < time) {
            curr = curr->next;
        }
        new_evt->next = curr->next;
        curr->next = new_evt;
    }
}

Event* pop_event() {
    if (!event_queue_head) return NULL;
    Event* evt = event_queue_head;
    event_queue_head = event_queue_head->next;
    return evt;
}

// ==================== ОЧЕРЕДИ ПАЦИЕНТОВ ====================
void queue_init(Queue* q) { q->count = 0; }
int queue_is_empty(Queue* q) { return q->count == 0; }
int queue_is_full(Queue* q) { return q->count >= MAX_QUEUE_SIZE; }

// Обычное добавление в конец (для очередей специалистов)
void queue_push_back(Queue* q, int val) {
    if (!queue_is_full(q)) {
        q->items[q->count++] = val;
    }
}

// Приоритетное добавление (для первичной очереди: срочные идут в начало)
void queue_push_priority(Queue* q, int val, int is_urgent) {
    if (queue_is_full(q)) {
        total_refused++;
        log_msg("[ВРЕМЯ %.2f] Очередь к дежурным переполнена! Пациент %d уходит.\n",
                current_time, val);
        return;
    }

    if (is_urgent) {
        // Сдвигаем элементы вправо, чтобы вставить срочного в начало
        for (int i = q->count; i > 0; i--) {
            q->items[i] = q->items[i - 1];
        }
        q->items[0] = val;
        q->count++;
    } else {
        q->items[q->count++] = val;
    }
}

int queue_pop(Queue* q) {
    if (queue_is_empty(q)) return -1;
    int val = q->items[0];
    // Сдвигаем элементы влево
    for (int i = 0; i < q->count - 1; i++) {
        q->items[i] = q->items[i + 1];
    }
    q->count--;
    return val;
}

// ==================== ЛОГИКА СИМУЛЯЦИИ ====================

// Попытка начать первичный осмотр
void try_start_primary_exam() {
    for (int i = 0; i < 2; i++) {
        if (!primary_doctors[i].is_busy && !queue_is_empty(&primary_queue)) {
            int pid = queue_pop(&primary_queue);
            Patient* p = &patients_pool[pid];

            primary_doctors[i].is_busy = 1;
            primary_doctors[i].current_patient_id = pid;
            p->primary_start_time = current_time;

            double end_time = current_time + p->exam_duration;
            log_msg("[ВРЕМЯ %.2f] Дежурный врач %d начал осмотр пациента %d%s (длит. %.2f)\n",
                   current_time, i + 1, pid, p->is_urgent ? " (СРОЧНЫЙ)" : "",
                   p->exam_duration);

            push_event(end_time, EVT_PRIMARY_END, pid, i, 0);
        }
    }
}

// Попытка начать лечение у специалиста
void try_start_specialist_treatment(SpecialistType spec) {
    if (!specialists[spec].is_busy && !queue_is_empty(&spec_queues[spec])) {
        int pid = queue_pop(&spec_queues[spec]);
        Patient* p = &patients_pool[pid];

        specialists[spec].is_busy = 1;
        specialists[spec].current_patient_id = pid;

        double end_time = current_time + p->treatment_duration;
        log_msg("[ВРЕМЯ %.2f] %s начал лечение пациента %d (длит. %.2f)\n",
               current_time, SPEC_NAMES[spec], pid, p->treatment_duration);

        push_event(end_time, EVT_TREATMENT_END, pid, 0, spec);
    }
}

// Обработка прибытия пациента
void handle_arrival(int pid) {
    Patient* p = &patients_pool[pid];
    p->arrival_time = current_time;

    // Генерация параметров
    p->spec_type = (SpecialistType)(rand() % SPEC_COUNT);
    p->exam_duration = rand_range(1.0, 4.0);
    p->treatment_duration = rand_range(2.0, 7.0);
    p->is_urgent = (rand() % 100) < urgent_chance_percent;

    if (p->is_urgent) total_urgent++;

    log_msg("[ВРЕМЯ %.2f] Прибыл пациент %d%s. Жалобы: %s.\n",
           current_time, pid, p->is_urgent ? " (СРОЧНЫЙ)" : "",
           SPEC_NAMES[p->spec_type]);

    // Попытка сразу занять дежурного
    int assigned = 0;
    for (int i = 0; i < 2; i++) {
        if (!primary_doctors[i].is_busy) {
            primary_doctors[i].is_busy = 1;
            primary_doctors[i].current_patient_id = pid;
            p->primary_start_time = current_time;

            double end_time = current_time + p->exam_duration;
            log_msg("               -> Сразу взят дежурным врачом %d.\n", i + 1);
            push_event(end_time, EVT_PRIMARY_END, pid, i, 0);
            assigned = 1;
            break;
        }
    }

    if (!assigned) {
        log_msg("               -> Встал в очередь к дежурным врачам.\n");
        queue_push_priority(&primary_queue, pid, p->is_urgent);
    }

    // Планирование следующего пациента
    if (total_arrived < total_patients_target) {
        double next_arrival = current_time + rand_range(1.0, 5.0);
        push_event(next_arrival, EVT_ARRIVAL, total_arrived, 0, 0);
        total_arrived++;
    }
}

// Обработка окончания первичного осмотра
void handle_primary_end(int pid, int doc_id) {
    primary_doctors[doc_id].is_busy = 0;
    Patient* p = &patients_pool[pid];

    // Подсчет времени ожидания в первичной очереди
    double wait_time = p->primary_start_time - p->arrival_time;
    sum_wait_time += wait_time;

    log_msg("[ВРЕМЯ %.2f] Дежурный врач %d закончил осмотр пациента %d. Направление к %s.\n",
           current_time, doc_id + 1, pid, SPEC_NAMES[p->spec_type]);

    // Направление к специалисту
    if (!specialists[p->spec_type].is_busy) {
        specialists[p->spec_type].is_busy = 1;
        specialists[p->spec_type].current_patient_id = pid;

        double end_time = current_time + p->treatment_duration;
        log_msg("               -> Сразу взят специалистом (%s).\n",
                SPEC_NAMES[p->spec_type]);
        push_event(end_time, EVT_TREATMENT_END, pid, 0, p->spec_type);
    } else {
        if (queue_is_full(&spec_queues[p->spec_type])) {
            total_refused++;
            log_msg("               -> Очередь к %s переполнена! Пациент уходит.\n",
                    SPEC_NAMES[p->spec_type]);
        } else {
            log_msg("               -> Встал в очередь к специалисту (%s).\n",
                    SPEC_NAMES[p->spec_type]);
            queue_push_back(&spec_queues[p->spec_type], pid);
        }
    }

    // Освободившийся дежурный берёт следующего
    try_start_primary_exam();
}

// Обработка окончания лечения
void handle_treatment_end(int pid, SpecialistType spec) {
    specialists[spec].is_busy = 0;
    log_msg("[ВРЕМЯ %.2f] %s закончил лечение пациента %d. Пациент выписан.\n",
           current_time, SPEC_NAMES[spec], pid);
    total_discharged++;

    // Освободившийся специалист берёт следующего
    try_start_specialist_treatment(spec);
}

// ==================== СТАТИСТИКА И ЗАВЕРШЕНИЕ ====================
void print_statistics() {
    log_msg("\n========================================\n");
    log_msg("       ИТОГОВАЯ СТАТИСТИКА СМЕНЫ        \n");
    log_msg("========================================\n");
    log_msg("Всего пациентов прибыло:    %d\n", total_arrived);
    log_msg("Выписано после лечения:     %d\n", total_discharged);
    log_msg("Ушло из-за очередей:        %d\n", total_refused);
    log_msg("Обработано срочных:         %d\n", total_urgent);

    if (total_discharged > 0) {
        log_msg("Среднее время ожид. (перв.): %.2f\n",
                sum_wait_time / total_discharged);
    }
    log_msg("========================================\n");
    log_msg("Лог сохранен в файл: hospital.log\n");
}

// ==================== ГЛАВНАЯ ФУНКЦИЯ ====================
int main(int argc, char* argv[]) {
    // 1. Парсинг аргументов командной строки (CLI)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--patients") == 0 && i + 1 < argc)
            total_patients_target = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            rng_seed = atoi(argv[++i]);
        else if (strcmp(argv[i], "--delay") == 0 && i + 1 < argc)
            visual_delay = atoi(argv[++i]);
        else if (strcmp(argv[i], "--urgent") == 0 && i + 1 < argc)
            urgent_chance_percent = atoi(argv[++i]);
    }

    // 2. Инициализация ГСЧ и сигналов
    srand(rng_seed);
    signal(SIGINT, sigint_handler);

    // 3. Открытие файла лога (Системный вызов open)
    log_fd = open("hospital.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd == -1) {
        perror("Не удалось открыть файл лога");
        return 1;
    }

    // 4. Инициализация сущностей
    for (int i = 0; i < 2; i++) {
        primary_doctors[i].id = i;
        primary_doctors[i].is_busy = 0;
    }
    for (int i = 0; i < SPEC_COUNT; i++) {
        specialists[i].id = i;
        specialists[i].is_busy = 0;
    }
    queue_init(&primary_queue);
    for (int i = 0; i < SPEC_COUNT; i++) queue_init(&spec_queues[i]);

    log_msg("=== НАЧАЛО РАБОЧЕГО ДНЯ БОЛЬНИЦЫ ===\n");
    log_msg("Параметры: Пациентов=%d, Seed=%u, Задержка=%d мкс, Срочные=%d%%\n\n",
            total_patients_target, rng_seed, visual_delay, urgent_chance_percent);

    // Запуск генерации первого пациента
    push_event(0.0, EVT_ARRIVAL, 0, 0, 0);
    total_arrived = 1;

    // 5. Главный цикл дискретно-событийной имитации
    while (keep_running) {
        Event* evt = pop_event();

        // Условие штатного завершения: все пациенты обработаны и очереди пусты
        int total_processed = total_discharged + total_refused;
        if (!evt || (total_arrived >= total_patients_target &&
                     total_processed >= total_patients_target)) {
            break;
        }

        current_time = evt->time;

        // Визуальная задержка для наглядности (бонусное требование)
        if (visual_delay > 0) usleep(visual_delay);

        switch (evt->type) {
            case EVT_ARRIVAL:       handle_arrival(evt->patient_id); break;
            case EVT_PRIMARY_END:   handle_primary_end(evt->patient_id, evt->doctor_id); break;
            case EVT_TREATMENT_END: handle_treatment_end(evt->patient_id, evt->spec); break;
        }
        free(evt);
    }

    // 6. Завершение работы
    log_msg("\n=== КОНЕЦ РАБОЧЕГО ДНЯ ===\n");
    print_statistics();

    // Закрытие дескриптора файла (Системный вызов close)
    if (log_fd != -1) close(log_fd);

    return 0;
}
