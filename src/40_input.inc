# =============================================================================
# poll_input — забрать все доступные байты stdin без ожидания.
# =============================================================================
poll_input:
.read_again:
    mov eax, SYS_read
    xor edi, edi                    # fd 0 = stdin
    lea rsi, [rip + input_byte]
    mov edx, 1
    syscall

    # Только rax==1 означает, что реально получен байт.
    cmp rax, 1
    jne .poll_done

    call process_input_byte

    # Продолжаем read, пока kernel не вернёт EAGAIN/0.
    jmp .read_again

.poll_done:
    ret

# =============================================================================
# process_input_byte — parser ANSI escape sequences.
# =============================================================================
# Стрелка в терминале приходит не «одним кодом», а тремя байтами:
#   ESC [ A = Up
#   ESC [ B = Down
#   ESC [ C = Right
#   ESC [ D = Left
#
# escape_state:
#   0 = обычный ввод
#   1 = уже увидели ESC
#   2 = уже увидели ESC [
# =============================================================================
process_input_byte:
    mov al, byte ptr [rip + input_byte]
    mov dl, byte ptr [rip + escape_state]

    cmp dl, 0
    je .state_zero
    cmp dl, 1
    je .state_one

    # state 2: ждём A/B/C/D и в любом случае возвращаем parser в state 0.
    mov byte ptr [rip + escape_state], 0

    # Во время GAME OVER стрелки игнорируются.
    cmp byte ptr [rip + game_over], 0
    jne .input_done

    cmp al, 'A'
    je rotate_action
    cmp al, 'B'
    je soft_drop_action
    cmp al, 'C'
    je move_right_action
    cmp al, 'D'
    je move_left_action
    jmp .input_done

.state_one:
    # После ESC нас интересует '['.
    cmp al, '['
    jne .reset

    mov byte ptr [rip + escape_state], 2
    ret

.reset:
    mov byte ptr [rip + escape_state], 0
    ret

.state_zero:
    # ESC начинает ANSI sequence.
    cmp al, 27
    jne .not_escape

    mov byte ptr [rip + escape_state], 1
    ret

.not_escape:
    # Q/q — единственная служебная клавиша помимо заданного управления.
    cmp al, 'q'
    je .request_quit
    cmp al, 'Q'
    je .request_quit

    # Space — hard drop, а после GAME OVER — restart.
    cmp al, ' '
    jne .input_done

    cmp byte ptr [rip + game_over], 0
    je hard_drop_action

    call init_game
    ret

.request_quit:
    mov byte ptr [rip + quit_requested], 1

.input_done:
    ret

# =============================================================================
# move_left_action
# =============================================================================
move_left_action:
    mov edi, dword ptr [rip + current_x]
    dec edi

    mov esi, dword ptr [rip + current_y]
    movzx edx, byte ptr [rip + current_rotation]
    call can_place

    test eax, eax
    jz .left_ret

    dec dword ptr [rip + current_x]
    mov byte ptr [rip + dirty], 1
.left_ret:
    ret

# =============================================================================
# move_right_action
# =============================================================================
move_right_action:
    mov edi, dword ptr [rip + current_x]
    inc edi

    mov esi, dword ptr [rip + current_y]
    movzx edx, byte ptr [rip + current_rotation]
    call can_place

    test eax, eax
    jz .right_ret

    inc dword ptr [rip + current_x]
    mov byte ptr [rip + dirty], 1
.right_ret:
    ret

# =============================================================================
# rotate_action — поворот по часовой стрелке.
# =============================================================================
# Для наглядности здесь нет сложного Super Rotation System/wall kicks.
# Поворот либо целиком допустим, либо не происходит.
# =============================================================================
rotate_action:
    # r12 — callee-saved register. Сохраняем его, потому что хотим пережить
    # вложенный call can_place и не потерять номер proposed rotation.
    push r12

    movzx r12d, byte ptr [rip + current_rotation]
    inc r12d
    and r12d, 3                      # modulo 4 через bit mask

    mov edi, dword ptr [rip + current_x]
    mov esi, dword ptr [rip + current_y]
    mov edx, r12d
    call can_place

    test eax, eax
    jz .rotate_ret

    mov byte ptr [rip + current_rotation], r12b
    mov byte ptr [rip + dirty], 1
.rotate_ret:
    pop r12
    ret

# =============================================================================
# soft_drop_action — Down.
# =============================================================================
# За каждый вручную пройденный вниз ряд добавляем 1 очко.
# =============================================================================
soft_drop_action:
    mov edi, dword ptr [rip + current_x]
    mov esi, dword ptr [rip + current_y]
    inc esi
    movzx edx, byte ptr [rip + current_rotation]
    call can_place

    test eax, eax
    jz .soft_lock

    inc dword ptr [rip + current_y]
    add qword ptr [rip + score], 1
    mov byte ptr [rip + dirty], 1
    ret

.soft_lock:
    call lock_piece
    ret

# =============================================================================
# hard_drop_action — Space.
# =============================================================================
# Сдвигаем фигуру вниз в tight loop, пока следующий ряд свободен.
# За каждый пройденный ряд — 2 очка, затем немедленная фиксация.
# =============================================================================
hard_drop_action:
    # can_place использует caller-saved r8-r11, поэтому счётчик drop distance
    # держим в callee-saved r12 и сохраняем его старое значение на stack.
    push r12
    xor r12d, r12d                    # r12d = сколько рядов пройдено

.drop_loop:
    mov edi, dword ptr [rip + current_x]
    mov esi, dword ptr [rip + current_y]
    inc esi
    movzx edx, byte ptr [rip + current_rotation]
    call can_place

    test eax, eax
    jz .landed

    inc dword ptr [rip + current_y]
    inc r12d
    jmp .drop_loop

.landed:
    # score += rows * 2.
    mov eax, r12d
    shl eax, 1
    add qword ptr [rip + score], rax

    # Восстанавливаем r12 ДО lock_piece: drop distance нам больше не нужен.
    pop r12
    call lock_piece
    ret

# =============================================================================
# gravity_step — автоматическое падение на одну клетку.
# =============================================================================
gravity_step:
    mov edi, dword ptr [rip + current_x]
    mov esi, dword ptr [rip + current_y]
    inc esi
    movzx edx, byte ptr [rip + current_rotation]
    call can_place

    test eax, eax
    jz .gravity_lock

    inc dword ptr [rip + current_y]
    mov byte ptr [rip + dirty], 1
    ret

.gravity_lock:
    call lock_piece
    ret

