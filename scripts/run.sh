#!/bin/sh
# Machine OS - 一键启动，用于人工观察引导效果
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# 用法：
#   ./scripts/run.sh                  构建并启动（弹出 QEMU 窗口）
#   ./scripts/run.sh -display none    额外参数原样传给 QEMU
#
# 窗口内快捷键：Ctrl+Alt+2 进入 QEMU 监视器，Ctrl+Alt+1 返回；关闭窗口即退出。

set -eu

cd "$(dirname "$0")/.."

IMG=build/machine-os.img

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "缺少依赖：$1（安装方式见 docs/0001-development-environment-report.md）" >&2
        exit 1
    }
}

need nasm
need make
need qemu-system-x86_64

printf 'Machine OS · %s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z')"

# make 自身处理增量构建，源码未改动时几乎瞬时完成
make

sleep 1

echo "启动 QEMU（镜像：${IMG}）"
echo "窗口内按 A 启动用户程序，按 B 退出"
# -boot order=c     强制从磁盘引导，跳过网络/光驱引导尝试
# -no-reboot        出错时不自动重启，避免不断循环，便于观察最后一屏
exec qemu-system-x86_64 \
    -m 256M \
    -boot order=c \
    -no-reboot \
    -drive format=raw,file="$IMG" \
    "$@"
