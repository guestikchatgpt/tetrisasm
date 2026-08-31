# =============================================================================
# is_cell_filled — что рисовать в конкретной клетке board.
# =============================================================================
# Вход:
#   edi = x, esi = y
# Выход:
#   eax = 1, если клетка занята board ИЛИ текущей падающей фигурой.
# =============================================================================
is_cell_filled:
    # Сначала постоянный board.
    imul eax, esi, BOARD_W
    add eax, edi
    lea r9, [rip + board]
    cmp byte ptr [r9 + rax], 0
    jne .cell_yes

    # Теперь 4 блока current piece.
    movzx eax, byte ptr [rip + current_type]
    shl eax, 5
    movzx ecx, byte ptr [rip + current_rotation]
    shl ecx, 3
    add eax, ecx

    lea r8, [rip + piece_cells]
    add r8, rax

    xor ecx, ecx

.scan_piece:
    movzx eax, byte ptr [r8 + rcx*2]
    add eax, dword ptr [rip + current_x]
    cmp eax, edi
    jne .cell_next

    movzx edx, byte ptr [r8 + rcx*2 + 1]
    add edx, dword ptr [rip + current_y]
    cmp edx, esi
    je .cell_yes

.cell_next:
    inc ecx
    cmp ecx, 4
    jl .scan_piece

    xor eax, eax
    ret

.cell_yes:
    mov eax, 1
    ret

# =============================================================================
# is_next_cell_filled — аналог для preview next piece, rotation 0.
# =============================================================================
# Вход: edi=x 0..3, esi=y 0..3
# Выход: eax=1/0
# =============================================================================
is_next_cell_filled:
    movzx eax, byte ptr [rip + next_type]
    shl eax, 5                        # rotation 0 => +0

    lea r8, [rip + piece_cells]
    add r8, rax

    xor ecx, ecx

.loop:
    movzx eax, byte ptr [r8 + rcx*2]
    cmp eax, edi
    jne .preview_next

    movzx edx, byte ptr [r8 + rcx*2 + 1]
    cmp edx, esi
    je .preview_yes

.preview_next:
    inc ecx
    cmp ecx, 4
    jl .loop

    xor eax, eax
    ret

.preview_yes:
    mov eax, 1
    ret

# =============================================================================
# append_bytes — добавить кусок текста в render_buffer.
# =============================================================================
# Вход:
#   rsi = адрес source
#   rdx = количество байт
#
# render_length — текущий конец кадра.
# =============================================================================
append_bytes:
    # Запоминаем исходную длину, потому что rep movsb уничтожит rcx.
    mov r8, rdx

    # destination = render_buffer + render_length.
    lea rdi, [rip + render_buffer]
    add rdi, qword ptr [rip + render_length]

    mov rcx, rdx
    rep movsb

    add qword ptr [rip + render_length], r8
    ret

# =============================================================================
# append_u64 — вывести unsigned 64-bit число в render_buffer.
# =============================================================================
# Вход:
#   rax = число
#
# Машина хранит число в binary. Терминалу нужны ASCII digits.
# Делим на 10, складываем остатки в обратном порядке, потом разворачиваем.
# =============================================================================
append_u64:
    # r8 = количество цифр.
    xor r8d, r8d

    # Частный будет в rax; делитель 10 держим в r9.
    mov r9, 10

    test rax, rax
    jnz .divide_loop

    # Отдельный случай для 0.
    mov byte ptr [rip + digit_buffer], '0'
    mov r8d, 1
    jmp .emit

.divide_loop:
    xor edx, edx
    div r9                              # rax=quotient, rdx=remainder

    add dl, '0'
    lea r10, [rip + digit_buffer]
    mov byte ptr [r10 + r8], dl
    inc r8

    test rax, rax
    jnz .divide_loop

.emit:
    # Записываем digits из digit_buffer в обратном порядке.
    mov rcx, r8

.emit_loop:
    dec rcx
    lea r10, [rip + digit_buffer]
    mov al, byte ptr [r10 + rcx]

    mov rdx, qword ptr [rip + render_length]
    lea r11, [rip + render_buffer]
    mov byte ptr [r11 + rdx], al
    inc rdx
    mov qword ptr [rip + render_length], rdx

    test rcx, rcx
    jnz .emit_loop

    ret

