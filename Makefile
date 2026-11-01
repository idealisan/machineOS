# Machine OS build system
# SPDX-License-Identifier: AGPL-3.0-or-later

ASM    := nasm
CC     := clang
LD     := ld.lld
QEMU   := qemu-system-x86_64

BUILD  := build
IMG    := $(BUILD)/machine-os.img
BOOT   := $(BUILD)/boot.bin
PAYLOAD:= $(BUILD)/payload.bin
USERBIN:= $(BUILD)/user.bin

OBJS := $(BUILD)/stage2.o $(BUILD)/main.o $(BUILD)/syscall.o \
        $(BUILD)/entry.o $(BUILD)/user_image.o

# 段间绝对/相对重定位告警：payload 最终链接到固定地址 0x7E00，
# 这些重定位在链接期必然可解析，非 -f bin 场景下无实际风险。
ASMFLAGS := -Wall -w-reloc-abs-word -w-reloc-abs-dword -w-reloc-abs-qword -w-reloc-rel-dword

# 裸机编译：不依赖宿主 libc / 运行时；关闭 red zone 与 SSE，
# 避免内核尚未初始化 FPU 时踩到调用者栈帧之外或触发 #NM。
CFLAGS := -target x86_64-elf -std=c17 -O2 -Wall -Wextra \
          -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -fno-asynchronous-unwind-tables \
          -mno-red-zone -mno-sse -mno-mmx -mno-80387

USER_CFLAGS := $(CFLAGS)

LDFLAGS := -T kernel/kernel.ld --image-base=0 --oformat=binary -nostdlib

.PHONY: all run clean

all: $(IMG)

$(BUILD):
	mkdir -p $(BUILD)

# ---- 用户态程序：独立编译、独立链接到 0x200000 ----
$(BUILD)/user.o: user/main.c | $(BUILD)
	$(CC) $(USER_CFLAGS) -c -o $@ $<

$(USERBIN): $(BUILD)/user.o user/user.ld
	$(LD) -T user/user.ld --image-base=0 --oformat=binary -nostdlib -o $@ $<

# 用户镜像以 incbin 方式嵌进内核只读段
$(BUILD)/user_image.o: kernel/user_image.asm $(USERBIN) | $(BUILD)
	$(ASM) -f elf64 -o $@ $<

# ---- 内核 ----
$(BUILD)/stage2.o: boot/stage2.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) -f elf64 -o $@ $<

$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) -f elf64 -o $@ $<

$(BUILD)/main.o: kernel/main.c kernel/io.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/syscall.o: kernel/syscall.c kernel/io.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# 链接后直接输出扁平的 payload（基址 0x7E00，不含引导扇区）
$(PAYLOAD): $(OBJS) kernel/kernel.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# 引导扇区需要知道要读多少扇区，所以在 payload 生成之后再汇编它
$(BOOT): boot/boot.asm $(PAYLOAD) | $(BUILD)
	@sectors=$$(( ($$(wc -c < $(PAYLOAD)) + 511) / 512 )); \
	 echo "payload: $$sectors sectors"; \
	 $(ASM) $(ASMFLAGS) -f bin -DSTAGE2_SECTORS=$$sectors -o $@ $<

# payload 末尾不一定落在扇区边界上，补零到整扇区，
# 否则 stage 1 按扇区读取时会读到镜像之外。
$(IMG): $(BOOT) $(PAYLOAD)
	cat $(BOOT) $(PAYLOAD) > $@
	@size=$$(wc -c < $@); pad=$$(( (512 - size % 512) % 512 )); \
	 if [ $$pad -ne 0 ]; then dd if=/dev/zero bs=1 count=$$pad >> $@ 2>/dev/null; fi; \
	 echo "image: $$(( $$(wc -c < $@) / 512 )) sectors"

run: $(IMG)
	./scripts/run.sh

clean:
	rm -rf $(BUILD)
