# 0001 — 开发环境与工具链调查报告

- 编号：0001
- 日期：2026-11-01
- 状态：调研完成，等待确认后安装缺失组件
- 范围：宿主开发机（ARM64 macOS）、目标平台（X86-64 传统 BIOS）交叉开发与模拟运行所需工具

## 1. 背景与目标

Machine OS 的目标平台是 X86-64 + 传统 BIOS（非 UEFI）。当前的开发机是一台 Apple
Silicon（ARM64）MacBook，指令集与目标平台不同，因此开发环境必须解决两件事：

1. **交叉编译**：在 ARM64 主机上产出一份能在 X86-64 上直接运行的代码，不带任何
   macOS 运行时依赖（freestanding，无 libc、无 dyld）。
2. **模拟运行**：用 QEMU 完整模拟 X86-64 机器，从 BIOS 加电开始跑我们的引导扇区与内核。

本报告记录实测结果、缺口、备选方案与推荐方案，作为后续搭建构建系统的依据。

## 2. 宿主环境实测

| 项目 | 实测值 |
| --- | --- |
| 机型 | Apple M1（arm64） |
| 操作系统 | macOS，Darwin Kernel 27.2.0 |
| 包管理器 | Homebrew 7.0.10（`/opt/homebrew`） |
| 架构 | arm64 |

注意：ARM64 宿主机无法用硬件虚拟化运行 X86 客户机（HVF 只支持同架构），
QEMU 只能走 **TCG 软件模拟**。这一点决定了后续调试与性能预期（见第 8 节）。

## 3. 工具需求清单

| 类别 | 工具 | 用途 |
| --- | --- | --- |
| 模拟器 | `qemu-system-x86_64` | 模拟 X86-64 机器、BIOS、磁盘、串口 |
| 汇编器 | `nasm` | 引导扇区与 16 位实模式代码（NASM 语法比 gas 更贴近裸机习惯） |
| 编译器 | C 编译器，支持 `-target x86_64-*-elf`、`-ffreestanding` | 编译内核 C 代码，输出 ELF64 |
| 链接器 | 支持 ELF64 + linker script 的 `ld` | 按链接脚本布局内核段，产出可加载映像 |
| 二进制工具 | `objcopy` / `nm` / `readelf` / `objdump` | 生成 flat binary、检查符号与段布局 |
| 构建 | `make`（或等价物） | 组织构建流程 |
| 脚本 | `python3` | 生成磁盘镜像、打包、自动化测试 |
| 调试 | GDB 协议客户端 | 通过 QEMU gdbstub 单步调试内核 |

## 4. 现状盘点（实测）

| 工具 | 状态 | 版本 / 路径 | 结论 |
| --- | --- | --- | --- |
| QEMU | ✅ 已安装 | 9.2.1，`/opt/homebrew/bin/qemu-system-x86_64` | 可用，含 x86_64 与 i386 目标 |
| NASM | ✅ 已安装 | 2.16.03 | 可用 |
| Clang | ✅ 已安装 | Apple clang 21.0.0，`/usr/bin/clang` | 可交叉编译到 `x86_64-elf`（已验证） |
| GNU make | ✅ 已安装 | 3.81（Xcode 版）、另有 `gmake` | 可用，本阶段足够 |
| Python | ✅ 已安装 | 3.14.6 | 可用 |
| Git | ✅ 已安装 | 2.48.1 | 可用 |
| **ELF 链接器** | ❌ 缺失 | Apple `ld` 只支持 Mach-O，且无 `ld.lld` | **阻塞项**，必须补齐 |
| **objcopy 等 ELF 工具** | ❌ 缺失 | Xcode 工具链只有 `llvm-nm`/`llvm-objdump`，无 `llvm-objcopy`、无 `readelf` | **阻塞项**，必须补齐 |
| GDB | ❌ 缺失 | 系统无 `gdb` | 非阻塞，可用 lldb 连 gdbstub 替代 |
| 交叉 GCC | ❌ 缺失 | 无 `x86_64-elf-gcc` | 非必需，见第 6 节方案对比 |
| mtools / xorriso / dosfstools | ❌ 缺失 | 未安装 | 本阶段不需要，做 FAT 镜像 / ISO 时再装 |

### 4.1 关于 `make` 的补充

`which make` 命中 Xcode 提供的 GNU Make 3.81（2006 年版本），另装有 `gmake`（GNU Make 4.x）。
本项目的 Makefile 不依赖 4.x 特性，两者皆可；如后续用到 `$(file ...)` 等新函数，
统一改用 `gmake` 或把 Homebrew 的 make 放到 PATH 前面。

## 5. 关键验证实验

### 5.1 交叉编译可行性（已通过）

Apple clang 虽然是 Apple 发行版，但保留了完整 LLVM 后端，可以直接指定 ELF 目标：

```
clang -target x86_64-elf -ffreestanding -fno-stack-protector \
      -fno-pic -mno-red-zone -c t.c -o t.o
```

结果：编译成功，`file t.o` 显示 `ELF 64-bit LSB relocatable, x86-64, version 1 (SYSV)`。
`-target x86_64-none-elf` 同样可用。结论：**不需要专门安装交叉 GCC 就能编译内核 C 代码**。

这意味着编译器缺口不存在，真正的缺口只在链接与二进制处理阶段。

### 5.2 QEMU 从 BIOS 启动（已通过）

用一个手写的 512 字节引导扇区做了最小化启动验证：

- 汇编为 flat binary（`nasm -f bin`），末尾 0xAA55 签名；
- 代码向调试端口 `0xE9` 输出一段文本；
- QEMU 以 raw 磁盘方式挂载该 512 字节文件，`-display none`，调试输出现已重定向到文件。

```
qemu-system-x86_64 -drive format=raw,file=boot.bin \
                   -display none -debugcon file:debug.log -no-reboot -m 128M
```

实测输出：`Machine OS: BIOS boot smoke test OK`。

结论：**QEMU 的 SeaBIOS 能正常引导自写引导扇区，整个"写代码 → 起 BIOS → 跑自己的代码"
闭环在当前机器上成立**。

### 5.3 链接环节（未通过，符合预期）

Apple `ld` 拒绝 ELF 输入（`ld: unknown options`/格式不支持），系统内也不存在 `ld.lld`。
因此当前**无法把多个 ELF 目标文件链接成内核映像**。这正是第 6 节要解决的问题。

### 5.4 遗留问题

- 首次冒烟测试同时使用了 `-device isa-debug-exit` 与 `-debugcon stdio`，QEMU 未按预期
  在 `out 0x501` 后自行退出，需靠外部 kill 结束。第二轮改用日志文件 + 外部看门狗则正常
  观察到输出。**isa-debug-exit 的退出行为需在搭建构建脚本时复测**，否则自动化的
  "运行即退出"测试会挂死。届时优先采用"外部超时看门狗 + 输出断言"的方式，不依赖退出码。

## 6. 方案对比与推荐

### 方案 A：安装 Homebrew 的 `llvm` 包（推荐）

提供 `ld.lld`、`llvm-objcopy`、`llvm-readelf`、`llvm-objdump`、`llvm-nm`、`clang`。

- 与已可用的 Apple clang 同属 LLVM 生态，编译与链接行为一致，不需要第二套编译前端；
- `ld.lld` 完整支持 linker script，能精确控制内核段布局（`ENTRY`、`AT`、`ALIGN`）；
- 一次安装同时解决链接器与二进制工具两个阻塞项；
- 代价：安装体积较大（数百 MB）。

### 方案 B：安装 `x86_64-elf-gcc` 交叉工具链（备选）

来自 `messense/macos-cross-toolchains` 等 tap，附带 binutils（`ld`、`objcopy`、`readelf`）。

- 优点：GNU 工具链，裸机教程与资料最丰富，行为最"传统"；
- 缺点：多一套编译器（GCC 与 clang 语义差异、内联汇编约束差异），安装还需引入第三方 tap；
- 适合后续需要 GNU 专属特性（如特定中断属性写法）时再补。

### 方案 C：只用 Apple 工具链 + 自写加载器

不现实：没有 ELF 链接器就无法做多目标文件链接，也无法生成可加载映像。

### 推荐

**方案 A**：`brew install llvm`，并在构建脚本中显式使用
`$(brew --prefix llvm)/bin/ld.lld` 与 `llvm-objcopy`。
编译器继续用系统 Apple clang（`-target x86_64-elf`），汇编器用 NASM。

理由：改动最小、组件最少、编译链接同源，且完全覆盖当前所有阻塞项。
若后续确实需要 GNU 语义，再按方案 B 追加，不影响已建立的流水线。

## 7. 计划采用的构建流水线

```
 boot.asm ──nasm -f bin──► boot.bin (512B, 0xAA55)  ─┐
                                                     ├─► dd 拼接到 disk.img
 kernel/*.c ──clang -target x86_64-elf -c──► *.o     │   (引导扇区在 LBA0)
 kernel/*.asm ──nasm -f elf64──► *.o                 │
                     │                               │
                     └─ld.lld -T kernel.ld──► kernel.elf ─llvm-objcopy─► kernel.bin
                                                     │
                                        disk.img ◄───┘
                                             │
                                             ▼
                        qemu-system-x86_64 -drive format=raw,file=disk.img
```

编译选项约定（本阶段确定）：

- `-target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone`
- `-mno-sse`（早期内核不启用 SSE，避免未初始化 FPU 状态）
- `-Wall -Wextra -std=c17`，内核代码不使用任何 libc 头文件

## 8. 模拟运行与调试方式

| 需求 | 方式 |
| --- | --- |
| 启动目标机 | `qemu-system-x86_64 -drive format=raw,file=disk.img -m 256M` |
| 无界面运行 | `-display none`（VGA 文本输出可后续通过 `-serial` 或转存显存获取） |
| 早期调试输出 | 端口 `0xE9` + `-debugcon file:...`（已实测可用） |
| 内核日志 | 串口 `COM1`（`-serial stdio`），内核实现 16550 驱动后启用 |
| 断点调试 | `-s -S` 启动 gdbstub，客户端用 `lldb` 连 `localhost:1234`（无需安装 gdb） |
| 加速 | **不可用**。ARM64 宿主只能 TCG 模拟 X86，无 KVM/HVF 加速，单步与外设访问都较慢 |
| 机器型号 | `-machine q35`（现代芯片组）或默认 i440fx（更贴近"传统 BIOS"生态） |

关于"传统 BIOS"的取舍：SeaBIOS 属于传统 BIOS 兼容实现，能覆盖我们目标场景
（实模式入口、`int 10h`/`int 13h`、VBE 等）；后续做 UEFI 无关性验证时再单独评估。

## 9. 风险与未验证项

1. `ld.lld` 尚未真正参与链接验证（未安装）。安装后第一件事是用 linker script
   链接一个最小 ELF 并检查 `PT_LOAD` 段与加载地址。
2. isa-debug-exit 的退出行为异常（见 5.4），自动化测试改为看门狗 + 输出断言。
3. QEMU TCG 下计时精度与真实硬件差异较大，后续调度器与时钟测试要在文档中明确
   "仅为模拟环境结论"。
4. Apple clang 的 `x86_64-elf` 目标属非官方主推组合，若遇到 intrinsic 或内联汇编
   兼容问题，回退方案为第 6 节方案 B。
5. 未验证 `-mno-sse` 下 clang 是否会生成 SSE 指令（需在反汇编中检查）。

## 10. 下一步行动

1. 执行 `brew install llvm`（需确认）。
2. 用 link script + `ld.lld` 链接最小内核 ELF，检查段布局。
3. 编写项目骨架：`Makefile`、`boot/boot.asm`、`kernel/`、`tools/`（镜像生成脚本）。
4. 打通"make run"一键启动到串口输出第一条日志。
5. 安装 mtools / xorriso / dosfstools 推迟到需要 FAT 文件系统与 ISO 时。

## 附录 A：本次验证使用的命令与产物

```
# 交叉编译
clang -target x86_64-elf -ffreestanding -fno-stack-protector -fno-pic \
      -mno-red-zone -c .tmp/t.c -o .tmp/t.o
file .tmp/t.o

# 引导扇区
nasm -f bin -o .tmp/boot.bin .tmp/boot.asm

# 启动验证
qemu-system-x86_64 -drive format=raw,file=.tmp/boot.bin \
      -display none -debugcon file:.tmp/debug.log -no-reboot -m 128M
cat .tmp/debug.log
```

验证产物保留在 `.tmp/`（按 AGENTS.md 规则三，暂不清理）。
