# Точка сборки исходника. GNU as текстово подставляет .inc-файлы ниже.
# Разбиение по подсистемам сделано только для удобства чтения: после .include
# assembler видит тот же единый поток assembly-кода.

.include "src/00_prelude.inc"
.include "src/10_state.inc"
.include "src/20_entry_terminal.inc"
.include "src/30_rng_game.inc"
.include "src/40_input.inc"
.include "src/50_lock_lines.inc"
.include "src/60_render_helpers.inc"
.include "src/70_render.inc"
.include "src/80_selftest.inc"
