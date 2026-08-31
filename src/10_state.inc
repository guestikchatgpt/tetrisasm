.section .bss
.align 8

# Игровое поле: 200 байт. 0 = пусто, 1 = занято зафиксированным блоком.
board:
    .skip BOARD_CELLS

# Текущая фигура и следующая фигура.
current_type:
    .skip 1
next_type:
    .skip 1
current_rotation:
    .skip 1

# Координаты текущей фигуры храним 32-битными signed integer.
current_x:
    .skip 4
current_y:
    .skip 4

# Игровая статистика.
.align 8
score:
    .skip 8
lines_total:
    .skip 4
level:
    .skip 4

# Флаги состояния.
game_over:
    .skip 1
quit_requested:
    .skip 1
dirty:
    .skip 1
escape_state:
    .skip 1

# Сколько миллисекунд накоплено до следующего автоматического падения.
gravity_accum:
    .skip 4

# Состояние простого PRNG xorshift32.
rng_state:
    .skip 4

# Один байт для неблокирующего чтения stdin.
input_byte:
    .skip 1

# Сохраняем исходный termios и отдельную рабочую копию.
# Реальная структура на Linux x86-64 занимает меньше 64 байт,
# но резервируем с запасом — так понятнее и безопаснее для копирования.
.align 8
termios_saved:
    .skip 64
termios_raw:
    .skip 64

# Исходные flags stdin, чтобы вернуть их перед выходом.
saved_stdin_flags:
    .skip 8

# Временный timespec для clock_gettime.
clock_ts:
    .skip 16

# Буфер кадра. Сначала строим весь экран здесь, потом делаем один write().
render_buffer:
    .skip 8192
render_length:
    .skip 8

# Буфер для преобразования числа в десятичный текст.
digit_buffer:
    .skip 32

# -----------------------------------------------------------------------------
# Секция .text: машинные инструкции программы.
# -----------------------------------------------------------------------------
