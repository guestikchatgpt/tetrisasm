.intel_syntax noprefix

# =============================================================================
# TETRIS ASM — учебный Tetris для Linux x86-64
# =============================================================================
#
# Это намеренно «многословный» исходник. Здесь комментариев гораздо больше,
# чем обычно оставляют в production-коде: цель проекта — показать человеку,
# который не знает ассемблер, как из очень простых инструкций складывается
# полноценная интерактивная программа.
#
# ВАЖНО: в программе НЕТ libc, ncurses, SDL и любого кода на C/C++.
# Мы собираем объектный файл напрямую GNU assembler'ом (as), а затем линкером
# ld превращаем его в ELF64. Всё общение с Linux идёт через системные вызовы.
#
# С точки зрения программы Tetris состоит из пяти больших частей:
#   1. Настройка терминала: отключаем канонический ввод и echo.
#   2. Чтение клавиатуры: read(0, ...), разбор ANSI-последовательностей стрелок.
#   3. Игровая логика: фигуры, столкновения, падение, фиксация, очистка линий.
#   4. Рендер: собираем текстовый кадр в памяти и write(1, ...).
#   5. Главный цикл: input -> gravity -> render -> nanosleep -> повтор.
#
# Для удобства обучения почти каждая нетривиальная инструкция подписана.
# =============================================================================

# -----------------------------------------------------------------------------
# Числовые константы Linux x86-64 syscall ABI.
#
# Когда выполняется инструкция `syscall`, ядро Linux смотрит:
#   rax = номер системного вызова
#   rdi = аргумент 1
#   rsi = аргумент 2
#   rdx = аргумент 3
#   r10 = аргумент 4
#   r8  = аргумент 5
#   r9  = аргумент 6
#
# Возвращаемое значение приходит в rax.
# Отрицательное rax означает -errno.
# -----------------------------------------------------------------------------
.equ SYS_read,          0
.equ SYS_write,         1
.equ SYS_ioctl,        16
.equ SYS_nanosleep,    35
.equ SYS_exit,         60
.equ SYS_fcntl,        72
.equ SYS_clock_gettime, 228

# ioctl-коды Linux для структуры termios.
.equ TCGETS, 0x5401
.equ TCSETS, 0x5402

# Команды fcntl.
.equ F_GETFL, 3
.equ F_SETFL, 4
.equ O_NONBLOCK, 0x800

# Биты c_lflag структуры termios.
.equ ISIG,   0x0001
.equ ICANON, 0x0002
.equ ECHO,   0x0008
.equ IEXTEN, 0x8000

# Биты c_iflag структуры termios.
.equ ICRNL, 0x0100
.equ IXON,  0x0400

# Размеры игрового поля.
.equ BOARD_W, 10
.equ BOARD_H, 20
.equ BOARD_CELLS, BOARD_W * BOARD_H

# Один цикл программы длится примерно 20 ms, то есть около 50 итераций/сек.
.equ FRAME_MS, 20
.equ FRAME_NS, 20000000

# -----------------------------------------------------------------------------
# Секция .rodata: неизменяемые данные.
# Здесь лежат строки интерфейса и геометрия всех тетромино.
# -----------------------------------------------------------------------------
.section .rodata

# ANSI escape sequences:
#   ESC [ H      — курсор в левый верхний угол
#   ESC [ 2 J    — очистить экран
#   ESC [ ?25l   — скрыть курсор
#   ESC [ ?25h   — показать курсор
ansi_start:
    .ascii "\033[2J\033[H\033[?25l"
.set ansi_start_len, . - ansi_start

ansi_home:
    .ascii "\033[H"
.set ansi_home_len, . - ansi_home

ansi_finish:
    .ascii "\033[?25h\033[0m\n"
.set ansi_finish_len, . - ansi_finish

msg_need_tty:
    .ascii "tetris: stdin must be a terminal (TTY)\n"
.set msg_need_tty_len, . - msg_need_tty

msg_selftest_ok:
    .ascii "SELF-TEST OK\n"
.set msg_selftest_ok_len, . - msg_selftest_ok

msg_selftest_fail:
    .ascii "SELF-TEST FAILED\n"
.set msg_selftest_fail_len, . - msg_selftest_fail

str_title:
    .ascii "TETRIS x86-64 / pure assembly / Linux syscalls\n"
.set str_title_len, . - str_title

str_top:
    .ascii "+--------------------+\n"
.set str_top_len, . - str_top

str_left:
    .ascii "|"
.set str_left_len, . - str_left

str_right_nl:
    .ascii "|\n"
.set str_right_nl_len, . - str_right_nl

str_block:
    .ascii "[]"
.set str_block_len, . - str_block

str_empty:
    .ascii "  "
.set str_empty_len, . - str_empty

str_score:
    .ascii "Score: "
.set str_score_len, . - str_score

str_lines:
    .ascii "Lines: "
.set str_lines_len, . - str_lines

str_level:
    .ascii "Level: "
.set str_level_len, . - str_level

str_next:
    .ascii "Next:\n"
.set str_next_len, . - str_next

str_next_indent:
    .ascii "    "
.set str_next_indent_len, . - str_next_indent

str_controls:
    .ascii "Controls: Left/Right = move, Down = soft drop, Up = rotate, Space = hard drop, Q = quit\n"
.set str_controls_len, . - str_controls

str_game_over:
    .ascii "GAME OVER — Space = new game, Q = quit\n"
.set str_game_over_len, . - str_game_over

str_nl:
    .ascii "\n"
.set str_nl_len, . - str_nl

# -----------------------------------------------------------------------------
# Таблица фигур.
#
# Мы не рисуем спрайты. Каждая фигура — просто 4 координаты (x, y).
# На каждую фигуру храним 4 поворота. На каждый поворот — 4 блока.
# Один блок занимает 2 байта: x и y.
#
# Итого на фигуру:
#     4 rotations * 4 blocks * 2 bytes = 32 bytes
#
# Поэтому адрес нужного поворота вычисляется так:
#     piece_cells + type*32 + rotation*8
#
# Нумерация типов:
#   0 = I, 1 = O, 2 = T, 3 = S, 4 = Z, 5 = J, 6 = L
# -----------------------------------------------------------------------------
piece_cells:
    # I piece ---------------------------------------------------------------
    .byte 0,1, 1,1, 2,1, 3,1      # rotation 0: ----
    .byte 2,0, 2,1, 2,2, 2,3      # rotation 1: vertical
    .byte 0,2, 1,2, 2,2, 3,2      # rotation 2
    .byte 1,0, 1,1, 1,2, 1,3      # rotation 3

    # O piece ---------------------------------------------------------------
    .byte 1,0, 2,0, 1,1, 2,1
    .byte 1,0, 2,0, 1,1, 2,1
    .byte 1,0, 2,0, 1,1, 2,1
    .byte 1,0, 2,0, 1,1, 2,1

    # T piece ---------------------------------------------------------------
    .byte 1,0, 0,1, 1,1, 2,1
    .byte 1,0, 1,1, 2,1, 1,2
    .byte 0,1, 1,1, 2,1, 1,2
    .byte 1,0, 0,1, 1,1, 1,2

    # S piece ---------------------------------------------------------------
    .byte 1,0, 2,0, 0,1, 1,1
    .byte 1,0, 1,1, 2,1, 2,2
    .byte 1,1, 2,1, 0,2, 1,2
    .byte 0,0, 0,1, 1,1, 1,2

    # Z piece ---------------------------------------------------------------
    .byte 0,0, 1,0, 1,1, 2,1
    .byte 2,0, 1,1, 2,1, 1,2
    .byte 0,1, 1,1, 1,2, 2,2
    .byte 1,0, 0,1, 1,1, 0,2

    # J piece ---------------------------------------------------------------
    .byte 0,0, 0,1, 1,1, 2,1
    .byte 1,0, 2,0, 1,1, 1,2
    .byte 0,1, 1,1, 2,1, 2,2
    .byte 1,0, 1,1, 0,2, 1,2

    # L piece ---------------------------------------------------------------
    .byte 2,0, 0,1, 1,1, 2,1
    .byte 1,0, 1,1, 1,2, 2,2
    .byte 0,1, 1,1, 2,1, 0,2
    .byte 0,0, 1,0, 1,1, 1,2

# Таблица очков за одновременно очищенные линии.
# Используем классическую понятную схему:
#   1 line  = 100 * level
#   2 lines = 300 * level
#   3 lines = 500 * level
#   4 lines = 800 * level
line_score_table:
    .long 0, 100, 300, 500, 800

# Аргумент nanosleep: struct timespec { tv_sec, tv_nsec }.
frame_sleep:
    .quad 0
    .quad FRAME_NS

# Строка для специального CI-режима.
selftest_arg:
    .ascii "--self-test\0"

# -----------------------------------------------------------------------------
# Секция .bss: память, которая при запуске автоматически обнулена ядром.
# Здесь находится всё изменяемое состояние игры.
# -----------------------------------------------------------------------------
