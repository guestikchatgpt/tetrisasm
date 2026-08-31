# =============================================================================
# render — собрать и показать один полный кадр.
# =============================================================================
render:
    # r12/r13 — callee-saved по SysV ABI. Мы используем их как y/x counters,
    # поэтому честно сохраняем старые значения на stack.
    push r12
    push r13

    mov qword ptr [rip + render_length], 0
    mov byte ptr [rip + dirty], 0

    # Перемещаем cursor домой, но экран каждый кадр не очищаем — меньше flicker.
    lea rsi, [rip + ansi_home]
    mov edx, ansi_home_len
    call append_bytes

    lea rsi, [rip + str_title]
    mov edx, str_title_len
    call append_bytes

    lea rsi, [rip + str_top]
    mov edx, str_top_len
    call append_bytes

    xor r12d, r12d                     # y = 0

.row:
    lea rsi, [rip + str_left]
    mov edx, str_left_len
    call append_bytes

    xor r13d, r13d                     # x = 0

.cell:
    mov edi, r13d
    mov esi, r12d
    call is_cell_filled

    test eax, eax
    jz .empty_cell

    lea rsi, [rip + str_block]
    mov edx, str_block_len
    call append_bytes
    jmp .after_cell

.empty_cell:
    lea rsi, [rip + str_empty]
    mov edx, str_empty_len
    call append_bytes

.after_cell:
    inc r13d
    cmp r13d, BOARD_W
    jl .cell

    lea rsi, [rip + str_right_nl]
    mov edx, str_right_nl_len
    call append_bytes

    inc r12d
    cmp r12d, BOARD_H
    jl .row

    lea rsi, [rip + str_top]
    mov edx, str_top_len
    call append_bytes

    # Score.
    lea rsi, [rip + str_score]
    mov edx, str_score_len
    call append_bytes
    mov rax, qword ptr [rip + score]
    call append_u64
    lea rsi, [rip + str_nl]
    mov edx, str_nl_len
    call append_bytes

    # Lines.
    lea rsi, [rip + str_lines]
    mov edx, str_lines_len
    call append_bytes
    mov eax, dword ptr [rip + lines_total]
    call append_u64
    lea rsi, [rip + str_nl]
    mov edx, str_nl_len
    call append_bytes

    # Level.
    lea rsi, [rip + str_level]
    mov edx, str_level_len
    call append_bytes
    mov eax, dword ptr [rip + level]
    call append_u64
    lea rsi, [rip + str_nl]
    mov edx, str_nl_len
    call append_bytes

    # Next piece label.
    lea rsi, [rip + str_next]
    mov edx, str_next_len
    call append_bytes

    # Preview — 4 rows x 4 columns, тоже двумя символами на block.
    xor r12d, r12d

.next_row:
    lea rsi, [rip + str_next_indent]
    mov edx, str_next_indent_len
    call append_bytes

    xor r13d, r13d

.next_cell:
    mov edi, r13d
    mov esi, r12d
    call is_next_cell_filled

    test eax, eax
    jz .next_empty

    lea rsi, [rip + str_block]
    mov edx, str_block_len
    call append_bytes
    jmp .next_after

.next_empty:
    lea rsi, [rip + str_empty]
    mov edx, str_empty_len
    call append_bytes

.next_after:
    inc r13d
    cmp r13d, 4
    jl .next_cell

    lea rsi, [rip + str_nl]
    mov edx, str_nl_len
    call append_bytes

    inc r12d
    cmp r12d, 4
    jl .next_row

    lea rsi, [rip + str_controls]
    mov edx, str_controls_len
    call append_bytes

    # Если game_over != 0, добавляем явное сообщение внизу.
    cmp byte ptr [rip + game_over], 0
    je .flush

    lea rsi, [rip + str_game_over]
    mov edx, str_game_over_len
    call append_bytes

.flush:
    # Один большой write вместо сотен маленьких — terminal I/O заметно дешевле.
    lea rsi, [rip + render_buffer]
    mov rdx, qword ptr [rip + render_length]
    call write_all

    pop r13
    pop r12
    ret

