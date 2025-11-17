# Epoll TCP/UDP Server

Неблокирующий сервер на C для GNU/Linux, который одновременно обрабатывает TCP и UDP подключения через `epoll`.  
Без сторонних библиотек, один бинарник, автотесты, нагрузочное тестирование, systemd unit и .deb пакет.

---

## Возможности

- Обработка **TCP** и **UDP** на одном порту.
- Один поток, **epoll** + неблокирующие сокеты.
- Линейный протокол: текстовые сообщения, команды начинаются с `/`.
- Поддерживаемые команды:
  - `/time` — вернуть текущее время сервера в формате `YYYY-MM-DD HH:MM:SS`.
  - `/stats` — статистика:
    - `total_tcp_clients` — всего TCP-клиентов за время жизни процесса;
    - `current_tcp_clients` — активных TCP-клиентов сейчас;
    - `total_udp_messages` — всего обработанных UDP сообщений.
  - `/shutdown` — мягко остановить сервер.
  - `/help` — вывести список команд.
- Обычные строки (без `/` в начале) эхоятся обратно.
- Логирование в stdout/stderr с таймштампами.
- Юнит-тесты логики протokола (`./tests`).
- Стресс-тест TCP-клиентов (`./stress`).
- Запуск через systemd (`server.service`).
- Простой `.deb` пакет (`epoll-server_1.0_amd64.deb`).

---

## Структура проекта

```text
.
├─ Makefile            # сборка server / tests / stress
├─ README.md
├─ packaging/
│  ├─ server.service   # systemd unit
│  └─ build_deb.sh     # сборка deb-пакета
└─ src/
   ├─ server.h         # API сервера
   ├─ server.c         # реализация epoll-сервера и протокола
   ├─ main.c           # точка входа (CLI)
   ├─ tests.c          # юнит-тесты server_process_line()
   └─ stress.c         # нагрузочный клиент
```

---

Требования
----------

- GCC или Clang (под Linux)
- make
- systemd (для запуска как сервиса)
- dpkg-deb (для сборки `.deb`)

---

Сборка
------

В корне проекта:

```bash
make
```

Соберутся три бинарника:

- `server` — сам сервер
- `tests` — юнит-тесты
- `stress` — нагрузочный клиент

Очистка:

```bash
make clean
```

---

Запуск сервера вручную
----------------------

По умолчанию порт берётся из аргумента:

```bash
./server 12345
```

Если порт не передать, будет использоваться `12345`:

```bash
./server
```

---

Протокол
--------

Все сообщения — текстовые строки, разделённые `\n` (по TCP — строка на строку, по UDP — один датаграм = одно сообщение).

### Обычные сообщения

Любое сообщение, **не начинающееся с `/`**, зеркалируется:

### Команды

- `/time`

  ```text
  client: /time
  server: 2025-11-17 01:23:45\n
  ```

- `/stats`

  ```text
  client: /stats
  server: total_tcp_clients=5 current_tcp_clients=2 total_udp_messages=12\n
  ```

- `/help`

  ```text
  client: /help
  server:
    Available commands:
    /time
    /stats
    /shutdown
    /help
  ```

- `/shutdown`

  ```text
  client: /shutdown
  server: shutting down\n
  ```

  После этого сервер корректно завершает работу:

  - закрывает все TCP-клиенты;
  - закрывает TCP/UDP сокеты;
  - выходит с кодом `0` (или `<0` при фатальной ошибке).

---

Примеры использования
---------------------

### TCP (через `nc`)

```bash
# терминал 1
./server 12345

# терминал 2
nc 127.0.0.1 12345
hello
/time
/stats
/help
/shutdown
```

### UDP (через `nc -u`)

```bash
echo -ne "/time\n"  | nc -u -w1 127.0.0.1 12345
echo -ne "/stats\n" | nc -u -w1 127.0.0.1 12345
```

---

Юнит-тесты
----------

Тестируется чистая функция:

```c
int server_process_line(
    const char *line,
    size_t len,
    const struct server_stats *stats,
    int *shutdown_requested,
    char *out,
    size_t out_cap
);
```

Покрытие:

- эхо с обрезкой пробелов;
- пустые строки и строки только с пробелами/`'\n'`;
- `/time` (проверка формата `YYYY-MM-DD HH:MM:SS`);
- `/stats` (разбор значений в строке);
- `/help` (наличие всех команд);
- `/shutdown` (установка флага и текст ответа);
- неизвестная команда `/foobar`;
- ошибки при маленьком выходном буфере.

Запуск:

```bash
./tests
```

Ожидаемый вывод:

```text
all tests passed
```

---

Нагрузочное тестирование
------------------------

Бинарник `stress` открывает несколько потоков, каждый поднимает TCP-соединение и шлёт пачки команд.

Сигнатура:

```bash
./stress host port threads messages_per_thread
```

Пример: 20 потоков, по 1000 сообщений на поток:

```bash
./server 12345 &
./stress 127.0.0.1 12345 20 1000
```

Каждый поток шлёт в цикле микс:

- обычные сообщения (`hello-<thread>-<i>`)
- `/time`
- `/stats`
- `/help`

---

Запуск через systemd
--------------------

Unit-файл лежит в `packaging/server.service`:

```ini
[Unit]
Description=Epoll TCP/UDP server
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/server 12345
Restart=on-failure
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target
```

### Установка вручную

```bash
# из корня проекта
sudo cp server /usr/local/bin/server
sudo chmod 755 /usr/local/bin/server

sudo cp packaging/server.service /etc/systemd/system/server.service
sudo systemctl daemon-reload
sudo systemctl enable server
sudo systemctl start server
sudo systemctl status server
```

Проверка логов:

```bash
sudo journalctl -u server.service -n 50 --no-pager
```

---

.deb пакет
----------

Скрипт сборки: `packaging/build_deb.sh`.

Он кладёт файлы сюда:

- бинарник: `/usr/local/bin/server`
- unit-файл: `/lib/systemd/system/server.service`

### Сборка пакета

```bash
# убедись, что проект НЕ на /mnt/c, а в нормальном каталоге, например:
#   ~/epoll-server
make
./packaging/build_deb.sh
```

После этого появится:

```text
epoll-server_1.0_amd64.deb
```

### Установка пакета

```bash
sudo dpkg -i epoll-server_1.0_amd64.deb
sudo systemctl daemon-reload
sudo systemctl enable server
sudo systemctl restart server
sudo systemctl status server
```
