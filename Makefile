# Сборка намеренно максимально прозрачная:
#   1) GNU assembler превращает tetris.s в ELF64 object;
#   2) GNU ld линкует его напрямую в executable.
# Никаких C-файлов, libc и скрытого startup runtime нет.

AS      := as
LD      := ld
ASFLAGS := --64 -g
LDFLAGS := -m elf_x86_64

BUILD_DIR := build
OBJECT    := $(BUILD_DIR)/tetris.o
BINARY    := $(BUILD_DIR)/tetris

.PHONY: all clean check run

all: $(BINARY)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJECT): tetris.s $(wildcard src/*.inc) | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -o $@ $<

$(BINARY): $(OBJECT)
	$(LD) $(LDFLAGS) -o $@ $<

# Self-test работает без TTY и потому подходит для GitHub Actions.
check: $(BINARY)
	./$(BINARY) --self-test

run: $(BINARY)
	./$(BINARY)

clean:
	rm -rf $(BUILD_DIR)
