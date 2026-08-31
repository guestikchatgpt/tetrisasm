# =============================================================================
# lock_piece — перенести 4 блока текущей фигуры в постоянный board.
# =============================================================================
lock_piece:
    # Находим адрес geometry текущего rotation.
    movzx eax, byte ptr [rip + current_type]
    shl eax, 5

    movzx ecx, byte ptr [rip + current_rotation]
    shl ecx, 3
    add eax, ecx

    lea r8, [rip + piece_cells]
    add r8, rax

    xor ecx, ecx

.copy_block:
    # absolute x.
    movzx eax, byte ptr [r8 + rcx*2]
    add eax, dword ptr [rip + current_x]

    # absolute y.
    movzx edx, byte ptr [r8 + rcx*2 + 1]
    add edx, dword ptr [rip + current_y]

    # Если блок выше видимой части, не пишем его в board.
    cmp edx, 0
    jl .lock_next

    # board[y*10+x] = 1.
    imul r9d, edx, BOARD_W
    add r9d, eax
    lea r10, [rip + board]
    mov byte ptr [r10 + r9], 1

.lock_next:
    inc ecx
    cmp ecx, 4
    jl .copy_block

    # Удаляем заполненные линии. eax = количество 0..4.
    call clear_lines

    # Сохраняем count в r8d, дальше вызовов функций до spawn_next нет.
    mov r8d, eax
    test r8d, r8d
    jz .spawn

    # score += line_score_table[count] * current_level.
    lea r9, [rip + line_score_table]
    mov eax, dword ptr [r9 + r8*4]
    imul eax, dword ptr [rip + level]
    add qword ptr [rip + score], rax

    # lines_total += count.
    add dword ptr [rip + lines_total], r8d

    # level = lines_total / 10 + 1.
    mov eax, dword ptr [rip + lines_total]
    xor edx, edx
    mov ecx, 10
    div ecx
    inc eax
    mov dword ptr [rip + level], eax

.spawn:
    call spawn_next
    mov byte ptr [rip + dirty], 1
    ret

# =============================================================================
# clear_lines — найти и удалить все заполненные строки.
# =============================================================================
# Выход:
#   eax = число очищенных строк 0..4
#
# Алгоритм идёт снизу вверх. Если строка заполнена:
#   - увеличиваем count;
#   - сдвигаем все строки над ней вниз на один;
#   - верхнюю строку очищаем;
#   - ТОТ ЖЕ y проверяем ещё раз, потому что туда приехала новая строка.
# =============================================================================
clear_lines:
    # rdi остаётся базовым адресом board на всё время функции.
    lea rdi, [rip + board]

    xor r8d, r8d                      # count = 0
    mov r9d, BOARD_H - 1              # y = 19

.row_loop:
    cmp r9d, 0
    jl .finished

    # Проверяем 10 клеток текущей строки.
    xor r10d, r10d                    # x = 0

.check_cell:
    imul eax, r9d, BOARD_W
    add eax, r10d

    cmp byte ptr [rdi + rax], 0
    je .row_not_full

    inc r10d
    cmp r10d, BOARD_W
    jl .check_cell

    # Все 10 клеток ненулевые — строка полная.
    inc r8d

    # dst_y = current y. Пока dst_y > 0, копируем row(dst_y-1) вниз.
    mov r10d, r9d

.shift_rows:
    cmp r10d, 0
    jle .clear_top

    # src_base = (dst_y - 1) * 10
    mov eax, r10d
    dec eax
    imul eax, eax, BOARD_W

    # dst_base = dst_y * 10
    mov edx, r10d
    imul edx, edx, BOARD_W

    # Копируем ровно 10 байт слева направо.
    xor ecx, ecx

.copy_row_byte:
    mov r11b, byte ptr [rdi + rax]
    mov byte ptr [rdi + rdx], r11b
    inc eax
    inc edx
    inc ecx
    cmp ecx, BOARD_W
    jl .copy_row_byte

    dec r10d
    jmp .shift_rows

.clear_top:
    # board[0..9] = 0.
    xor ecx, ecx

.clear_top_loop:
    mov byte ptr [rdi + rcx], 0
    inc ecx
    cmp ecx, BOARD_W
    jl .clear_top_loop

    # НЕ уменьшаем y: проверяем строку, приехавшую сверху.
    jmp .row_loop

.row_not_full:
    dec r9d
    jmp .row_loop

.finished:
    mov eax, r8d
    ret

# =============================================================================
# get_gravity_interval
# =============================================================================
# Уровень повышается каждые 10 линий.
# Скорость — намеренно простая и видимая в коде:
#   level 1: 800 ms
#   level 2: 740 ms
#   ...
#   минимум: 100 ms
# =============================================================================
get_gravity_interval:
    mov eax, dword ptr [rip + level]
    dec eax

    # (level-1) * 60
    imul eax, eax, 60

    # 800 - result
    mov ecx, 800
    sub ecx, eax
    mov eax, ecx

    cmp eax, 100
    jge .gravity_done
    mov eax, 100

.gravity_done:
    ret

