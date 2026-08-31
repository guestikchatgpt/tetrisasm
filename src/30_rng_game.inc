# =============================================================================
# seed_rng — начальное состояние xorshift32.
# =============================================================================
seed_rng:
    # clock_gettime(CLOCK_MONOTONIC=1, &clock_ts)
    mov eax, SYS_clock_gettime
    mov edi, 1
    lea rsi, [rip + clock_ts]
    syscall

    # Если syscall неожиданно не сработал, используем ненулевую константу.
    test rax, rax
    js .fallback

    # Смешиваем младшие 32 бита секунд и наносекунд.
    mov eax, dword ptr [rip + clock_ts]
    xor eax, dword ptr [rip + clock_ts + 8]

    # xorshift32 не должен иметь state=0: ноль навсегда останется нулём.
    test eax, eax
    jnz .store

.fallback:
    mov eax, 0x6D2B79F5

.store:
    mov dword ptr [rip + rng_state], eax
    ret

# =============================================================================
# random_piece — получить псевдослучайный тип 0..6.
# =============================================================================
# Алгоритм xorshift32 очень мал и удобен для демонстрации bitwise-инструкций.
# Он НЕ криптографический и для игры этого более чем достаточно.
# =============================================================================
random_piece:
    mov eax, dword ptr [rip + rng_state]

    # x ^= x << 13
    mov edx, eax
    shl edx, 13
    xor eax, edx

    # x ^= x >> 17
    mov edx, eax
    shr edx, 17
    xor eax, edx

    # x ^= x << 5
    mov edx, eax
    shl edx, 5
    xor eax, edx

    mov dword ptr [rip + rng_state], eax

    # Делим unsigned 32-bit x на 7.
    # div ecx делит EDX:EAX; остаток оказывается в EDX.
    xor edx, edx
    mov ecx, 7
    div ecx

    # Остаток всегда 0..6 — это и есть тип фигуры.
    mov eax, edx
    ret

# =============================================================================
# init_game — полностью начать игру заново.
# =============================================================================
init_game:
    # board[0..199] = 0.
    lea rdi, [rip + board]
    xor eax, eax
    mov ecx, BOARD_CELLS
    rep stosb

    # Обнуляем статистику и runtime state.
    mov qword ptr [rip + score], 0
    mov dword ptr [rip + lines_total], 0
    mov dword ptr [rip + level], 1
    mov dword ptr [rip + gravity_accum], 0
    mov byte ptr [rip + game_over], 0
    mov byte ptr [rip + escape_state], 0

    # Выбираем текущую фигуру.
    call random_piece
    mov byte ptr [rip + current_type], al

    # И сразу следующую — для preview и будущего spawn.
    call random_piece
    mov byte ptr [rip + next_type], al

    # Стандартная стартовая позиция внутри 10-column board.
    mov byte ptr [rip + current_rotation], 0
    mov dword ptr [rip + current_x], 3
    mov dword ptr [rip + current_y], 0

    mov byte ptr [rip + dirty], 1
    ret

# =============================================================================
# spawn_next — сделать next piece текущей и сгенерировать новую next.
# =============================================================================
spawn_next:
    mov al, byte ptr [rip + next_type]
    mov byte ptr [rip + current_type], al

    call random_piece
    mov byte ptr [rip + next_type], al

    mov byte ptr [rip + current_rotation], 0
    mov dword ptr [rip + current_x], 3
    mov dword ptr [rip + current_y], 0
    mov dword ptr [rip + gravity_accum], 0

    # Проверяем, помещается ли новая фигура в spawn position.
    mov edi, 3
    xor esi, esi
    xor edx, edx
    call can_place

    test eax, eax
    jnz .ok

    # Если клетки уже заняты, игра закончена.
    mov byte ptr [rip + game_over], 1

.ok:
    mov byte ptr [rip + dirty], 1
    ret

# =============================================================================
# can_place — collision detection.
# =============================================================================
# Вход:
#   edi = proposed x
#   esi = proposed y
#   edx = proposed rotation (0..3)
# Использует current_type как тип проверяемой фигуры.
#
# Выход:
#   eax = 1 — фигура помещается
#   eax = 0 — столкновение со стеной, полом или board
# =============================================================================
can_place:
    # Вычисляем offset = current_type*32 + rotation*8.
    movzx eax, byte ptr [rip + current_type]
    shl eax, 5                    # *32

    mov ecx, edx
    shl ecx, 3                    # *8
    add eax, ecx

    # r8 указывает на 8 байт координат нужного поворота.
    lea r8, [rip + piece_cells]
    add r8, rax

    # ecx = индекс блока фигуры: 0..3.
    xor ecx, ecx

.check_block:
    # block_x = proposed_x + local_x.
    movzx eax, byte ptr [r8 + rcx*2]
    add eax, edi

    # Левая/правая границы поля.
    cmp eax, 0
    jl .collision
    cmp eax, BOARD_W
    jge .collision

    # block_y = proposed_y + local_y.
    movzx r9d, byte ptr [r8 + rcx*2 + 1]
    add r9d, esi

    # Ниже нижней границы — collision.
    cmp r9d, BOARD_H
    jge .collision

    # Координаты выше 0 разрешаем как пустое пространство.
    # Текущие таблицы их не используют, но такой can_place универсальнее.
    cmp r9d, 0
    jl .next_block

    # board index = y*10 + x.
    imul r10d, r9d, BOARD_W
    add r10d, eax

    # Любое ненулевое значение означает уже зафиксированный блок.
    lea r11, [rip + board]
    cmp byte ptr [r11 + r10], 0
    jne .collision

.next_block:
    inc ecx
    cmp ecx, 4
    jl .check_block

    mov eax, 1
    ret

.collision:
    xor eax, eax
    ret

