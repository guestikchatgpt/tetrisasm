# =============================================================================
# run_selftest — небольшой CI-тест игровой логики без терминала.
# =============================================================================
# Цель не доказать корректность всего Tetris, а поймать грубые поломки:
#   - can_place должен отличать свободное место от выхода за стену;
#   - clear_lines должен удалить полностью заполненный нижний ряд.
# =============================================================================
run_selftest:
    # Обнуляем board.
    lea rdi, [rip + board]
    xor eax, eax
    mov ecx, BOARD_CELLS
    rep stosb

    # Проверяем O-piece в центре.
    mov byte ptr [rip + current_type], 1
    mov byte ptr [rip + current_rotation], 0
    mov dword ptr [rip + current_x], 3
    mov dword ptr [rip + current_y], 0

    mov edi, 3
    xor esi, esi
    xor edx, edx
    call can_place
    cmp eax, 1
    jne .fail

    # Та же фигура слишком далеко слева должна collide.
    mov edi, -2
    xor esi, esi
    xor edx, edx
    call can_place
    test eax, eax
    jne .fail

    # Заполняем нижний ряд board[190..199].
    lea rdi, [rip + board + 190]
    mov al, 1
    mov ecx, BOARD_W
    rep stosb

    call clear_lines
    cmp eax, 1
    jne .fail

    # После clear нижний ряд должен быть снова пуст.
    xor ecx, ecx

.check_bottom:
    lea r8, [rip + board + 190]
    cmp byte ptr [r8 + rcx], 0
    jne .fail
    inc ecx
    cmp ecx, BOARD_W
    jl .check_bottom

    # Всё прошло.
    mov edi, 1
    lea rsi, [rip + msg_selftest_ok]
    mov edx, msg_selftest_ok_len
    call write_all_fd

    mov eax, SYS_exit
    xor edi, edi
    syscall

.fail:
    mov edi, 2
    lea rsi, [rip + msg_selftest_fail]
    mov edx, msg_selftest_fail_len
    call write_all_fd

    mov eax, SYS_exit
    mov edi, 1
    syscall
