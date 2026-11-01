# Machine OS build system
# SPDX-License-Identifier: AGPL-3.0-or-later

ASM    := nasm
QEMU   := qemu-system-x86_64
BUILD  := build
IMG    := $(BUILD)/machine-os.img
BOOT   := $(BUILD)/boot.bin

# -w-reloc-abs-word：flat binary 下所有绝对地址都在汇编期解析，且均在 16 位范围内，
# 该告警只针对可重定位输出的潜在截断，本场景不适用。
ASMFLAGS := -Wall -w-reloc-abs-word

.PHONY: all run clean

all: $(IMG)

$(BUILD):
	mkdir -p $(BUILD)

$(BOOT): boot/boot.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) -f bin -o $@ $<

# 当前镜像只有一个引导扇区；后续阶段会把内核追加到扇区之后
$(IMG): $(BOOT)
	cp $(BOOT) $@

run: $(IMG)
	$(QEMU) -m 256M -drive format=raw,file=$(IMG)

clean:
	rm -rf $(BUILD)
