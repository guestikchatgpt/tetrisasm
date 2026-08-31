.section .text
.global _start

# =============================================================================
# _start — настоящая точка входа ELF.
# =============================================================================
# В обычной программе на C до main() существует стартовый код libc.
# У нас libc нет, поэтому Linux передаёт управление сразу сюда.
#
# На вершине стека при входе лежит argc, далее указатели argv[].
# Мы используем это только для `--self-test`, чтобы GitHub Actions мог проверить
# логику без интерактивного терминала.
# =============================================================================
_start:
    # argc находится по адресу [rsp].
    mov rax, qword ptr [rsp]

    # Если argc == 2, проверим argv[1] на строку "--self-test".
    cmp rax, 2
    jne .normal_start

    # argv[1] расположен через 16 байт от начального rsp:
    # [rsp] = argc, [rsp+8] = argv[0], [rsp+16] = argv[1].
    mov rdi, qword ptr [rsp + 16]
    call arg_is_selftest
    test eax, eax
    jz .normal_start

    # В self-test терминал вообще не трогаем.
    call run_selftest

.normal_start:
    # Настраиваем stdin как неблокирующий терминал без echo/canonical mode.
    call setup_terminal

    # Сеем генератор псевдослучайных фигур текущим monotonic-временем.
    call seed_rng

    # Создаём чистое поле и первую пару фигур.
    call init_game

    # Первый кадр обязательно нужно отрисовать.
    mov byte ptr [rip + dirty], 1

# =============================================================================
# Главный цикл игры.
# =============================================================================
# Он специально сделан очень прямолинейно:
#   1. прочитать все накопившиеся клавиши;
#   2. если пришёл Q — выйти;
#   3. если игра идёт — накопить 20 ms для gravity;
#   4. при достижении интервала сдвинуть фигуру вниз;
#   5. если что-то изменилось — перерисовать экран;
#   6. поспать 20 ms;
#   7. повторить.
# =============================================================================
main_loop:
    call poll_input

    # Проверяем флаг выхода после обработки клавиатуры.
    cmp byte ptr [rip + quit_requested], 0
    jne clean_exit

    # На экране GAME OVER gravity больше не нужна.
    cmp byte ptr [rip + game_over], 0
    jne .maybe_render

    # Каждый оборот главного цикла добавляет FRAME_MS к счётчику gravity.
    add dword ptr [rip + gravity_accum], FRAME_MS

    # Получаем текущий интервал падения в миллисекундах.
    call get_gravity_interval

    # Если накоплено меньше интервала — фигуру пока не двигаем.
    mov ecx, dword ptr [rip + gravity_accum]
    cmp ecx, eax
    jb .maybe_render

    # Вычитаем один интервал, а не просто обнуляем accumulator.
    # Это уменьшает дрейф таймера при небольших задержках кадра.
    sub dword ptr [rip + gravity_accum], eax

    # Один автоматический шаг вниз; при столкновении фигура зафиксируется.
    call gravity_step

.maybe_render:
    # Рендер нужен только тогда, когда состояние визуально изменилось.
    cmp byte ptr [rip + dirty], 0
    je .sleep

    call render

.sleep:
    # nanosleep(NULL не нужен): передаём адрес constant timespec в rdi,
    # второй аргумент rsi = 0 означает «остаток сна не сохранять».
    mov eax, SYS_nanosleep
    lea rdi, [rip + frame_sleep]
    xor esi, esi
    syscall

    jmp main_loop

# =============================================================================
# clean_exit — аккуратный выход.
# =============================================================================
clean_exit:
    # Возвращаем terminal flags и termios, чтобы shell снова работал нормально.
    call restore_terminal

    # exit(0)
    mov eax, SYS_exit
    xor edi, edi
    syscall

# =============================================================================
# arg_is_selftest
# =============================================================================
# Вход:
#   rdi = char* argv[1]
# Выход:
#   eax = 1, если строка ровно "--self-test"
#   eax = 0 иначе
# =============================================================================
arg_is_selftest:
    lea rsi, [rip + selftest_arg]

.compare_loop:
    # Загружаем по одному байту из argv и эталона.
    mov al, byte ptr [rdi]
    mov dl, byte ptr [rsi]

    # Любое различие означает false.
    cmp al, dl
    jne .arg_no

    # Если оба байта равны нулю, обе строки закончились одновременно.
    test al, al
    je .arg_yes

    inc rdi
    inc rsi
    jmp .compare_loop

.arg_yes:
    mov eax, 1
    ret

.arg_no:
    xor eax, eax
    ret

# =============================================================================
# setup_terminal
# =============================================================================
# Делает три вещи:
#   1. TCGETS — сохраняет настройки терминала;
#   2. TCSETS — отключает canonical/echo/signal processing;
#   3. fcntl — включает O_NONBLOCK для stdin.
#
# Почему canonical mode мешает игре?
# В canonical mode read() отдаёт ввод только после Enter. Игре нужно получать
# стрелку сразу, поэтому canonical mode отключается.
# =============================================================================
setup_terminal:
    # ioctl(0, TCGETS, &termios_saved)
    mov eax, SYS_ioctl
    xor edi, edi
    mov esi, TCGETS
    lea rdx, [rip + termios_saved]
    syscall

    # Отрицательное rax = ошибка (например stdin не TTY).
    test rax, rax
    js .not_a_tty

    # Копируем первые 64 байта saved -> raw.
    # rep movsb повторяет копирование rcx раз.
    lea rsi, [rip + termios_saved]
    lea rdi, [rip + termios_raw]
    mov ecx, 64
    rep movsb

    # termios.c_lflag находится по offset 12 на Linux x86-64.
    # Убираем ICANON, ECHO, ISIG и IEXTEN.
    # ISIG отключён намеренно: Ctrl+C не должен убить процесс до того,
    # как мы восстановим terminal settings. Выход из игры — клавиша Q.
    mov eax, dword ptr [rip + termios_raw + 12]
    and eax, ~(ICANON | ECHO | ISIG | IEXTEN)
    mov dword ptr [rip + termios_raw + 12], eax

    # c_iflag offset 0. Отключаем преобразование CR->NL и software flow control.
    mov eax, dword ptr [rip + termios_raw]
    and eax, ~(ICRNL | IXON)
    mov dword ptr [rip + termios_raw], eax

    # c_cc начинается с offset 17.
    # VTIME = c_cc[5] => offset 22, VMIN = c_cc[6] => offset 23.
    # Оба нуля: read() сам по себе не должен ждать байт.
    mov byte ptr [rip + termios_raw + 22], 0
    mov byte ptr [rip + termios_raw + 23], 0

    # ioctl(0, TCSETS, &termios_raw)
    mov eax, SYS_ioctl
    xor edi, edi
    mov esi, TCSETS
    lea rdx, [rip + termios_raw]
    syscall

    # Получаем текущие file status flags stdin.
    mov eax, SYS_fcntl
    xor edi, edi
    mov esi, F_GETFL
    xor edx, edx
    syscall

    # Сохраняем их целиком для последующего restore.
    mov qword ptr [rip + saved_stdin_flags], rax

    # Включаем O_NONBLOCK, сохранив все остальные flags.
    mov rdx, rax
    or rdx, O_NONBLOCK
    mov eax, SYS_fcntl
    xor edi, edi
    mov esi, F_SETFL
    syscall

    # Один раз очищаем экран и скрываем текстовый cursor.
    lea rsi, [rip + ansi_start]
    mov edx, ansi_start_len
    call write_all

    ret

.not_a_tty:
    # Печатаем объяснение в stderr (fd 2).
    mov edi, 2
    lea rsi, [rip + msg_need_tty]
    mov edx, msg_need_tty_len
    call write_all_fd

    # exit(1)
    mov eax, SYS_exit
    mov edi, 1
    syscall

# =============================================================================
# restore_terminal
# =============================================================================
restore_terminal:
    # Сначала возвращаем исходные stdin flags.
    mov eax, SYS_fcntl
    xor edi, edi
    mov esi, F_SETFL
    mov rdx, qword ptr [rip + saved_stdin_flags]
    syscall

    # Затем возвращаем исходный termios.
    mov eax, SYS_ioctl
    xor edi, edi
    mov esi, TCSETS
    lea rdx, [rip + termios_saved]
    syscall

    # Показываем cursor и переводим строку.
    lea rsi, [rip + ansi_finish]
    mov edx, ansi_finish_len
    call write_all

    ret

# =============================================================================
# write_all
# =============================================================================
# Удобная обёртка для stdout.
# Вход:
#   rsi = buffer
#   rdx = length
# =============================================================================
write_all:
    mov edi, 1
    jmp write_all_fd

# =============================================================================
# write_all_fd
# =============================================================================
# Linux write() имеет право записать меньше байт, чем мы попросили.
# Для терминала это редко заметно, но правильный код обязан учитывать partial
# write, поэтому здесь есть цикл.
#
# Вход:
#   rdi = fd
#   rsi = buffer
#   rdx = length
# =============================================================================
write_all_fd:
    # Сохраняем fd в r8: syscall не портит r8.
    mov r8, rdi

.write_loop:
    test rdx, rdx
    jz .write_done

    mov eax, SYS_write
    mov rdi, r8
    syscall

    # rax <= 0: либо ошибка, либо нечего продолжать.
    test rax, rax
    jle .write_done

    # Сдвигаем указатель и уменьшаем оставшуюся длину.
    add rsi, rax
    sub rdx, rax
    jmp .write_loop

.write_done:
    ret

