; 把构建出的用户程序嵌进内核镜像
; SPDX-License-Identifier: AGPL-3.0-or-later

section .rodata
global user_image_start
global user_image_end

user_image_start:
    incbin "build/user.bin"
user_image_end:
