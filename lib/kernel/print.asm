TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

[bits 32]

section .data
put_int_buffer dq 0    ; 定义 8 字节缓冲区用于数字到字符的转换
put_hex_buffer dq 0    ; 十六进制转换缓冲区（8字节足够存储 0xFFFFFFFF）

section .text
;------------------------ put_char -----------------------------
;功能描述：把栈中的 1 个字符写入光标所在处
;-------------------------------------------------------------------
global put_char
put_char:
    pushad        ;备份 32 位寄存器环境
    ;需要保证 gs 中为正确的视频段选择子
    ;为保险起见，每次打印时都为 gs 赋值
    mov ax, SELECTOR_VIDEO    ; 不能直接把立即数送入段寄存器
    mov gs, ax

    ;;;;;;;;; 获取当前光标位置 ;;;;;;;;;
    ;先获得高 8 位
    mov dx, 0x03d4    ;索引寄存器
    mov al, 0x0e      ;用于提供光标位置的高 8 位
    out dx, al
    mov dx, 0x03d5    ;通过读写数据端口 0x3d5 来获得或设置光标位置
    in al, dx         ;得到了光标位置的高 8 位
    mov ah, al

    ;再获取低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    in al, dx

    ;将光标存入 bx
    mov bx, ax
    ;下面这行是在栈中获取待打印的字符
    mov ecx, [esp + 36]    ;pushad 压入 4×8＝32 字节，加上主调函数 4 字节的返回地址，故 esp+36 字节
    cmp cl, 0xd            ;CR 是 0x0d，LF 是 0x0a
    jz .is_carriage_return
    cmp cl, 0xa
    jz .is_line_feed

    cmp cl, 0x8            ;BS(backspace)的 asc 码是 8
    jz .is_backspace
    jmp .put_other

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 处理特殊字符
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.is_backspace:
    cmp bx, 0              ; 检查光标是否在屏幕最开始
    je .backspace_done     ; 如果在开头，无法删除
    
    dec bx                 ; 光标向前移动一位
    
    ; 保存当前光标位置（要删除的位置）
    push bx
    
    ; 计算从当前位置到行尾需要移动的字符数
    ; 每行 80 个字符，计算当前位置到行尾还有多少字符
    mov ax, bx
    mov dx, 0
    mov cx, 80
    div cx                 ; 除法 , 余数在dx. ax = 行号, dx = 列号
    
    mov cx, 80
    sub cx, dx             ; cx = 行内剩余字符数
    dec cx                 ; 减去当前位置，得到需要移动的字符数
    
    ; 如果是行尾，只需清空当前字符
    cmp cx, 0
    jle .just_clear_char
    
    ; 将后面的字符向前移动
    mov si, bx             ; si = 当前位置
    inc si                 ; si = 下一个字符位置
    mov di, bx             ; di = 当前位置（目标）
    
.move_chars:
    ; 读取下一个字符及其属性
    shl si, 1              ; si * 2 = 显存偏移
    mov ax, [gs:si]        ; 读取字符和属性（2字节）
    shr si, 1              ; 恢复 si
    
    ; 写入当前位置
    shl di, 1              ; di * 2 = 显存偏移
    mov [gs:di], ax        ; 写入字符和属性
    shr di, 1              ; 恢复 di
    
    inc si                 ; 下一个源位置
    inc di                 ; 下一个目标位置
    
    ; 检查是否到行尾
    mov ax, di
    mov dx, 0
    push cx
    mov cx, 80
    div cx                 ; dx = 列号
    pop cx
    cmp dx, 79             ; 是否到行尾
    je .clear_last_char
    
    loop .move_chars
    
.clear_last_char:
    ; 清空最后一个字符位置（原来最后一个字符的位置）
    shl di, 1
    mov byte [gs:di], 0x20     ; 空格
    inc di
    mov byte [gs:di], 0x07     ; 默认属性
    jmp .backspace_restore

.just_clear_char:
    ; 只清空当前字符
    shl bx, 1
    mov byte [gs:bx], 0x20     ; 空格
    inc bx
    mov byte [gs:bx], 0x07     ; 默认属性
    
.backspace_restore:
    pop bx                 ; 恢复光标位置
    
.backspace_done:
    jmp .set_cursor

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 普通字符输出
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.put_other:
    shl bx, 1              ; 光标位置 * 2 = 显存偏移
    mov [gs:bx], cl        ; 写入字符
    inc bx
    mov byte [gs:bx], 0x07 ; 字符属性（白色前景，黑色背景）
    shr bx, 1              ; 恢复光标值
    inc bx                 ; 移动到下一个位置
    cmp bx, 2000           ; 检查是否超出屏幕（25行*80列=2000）
    jl .set_cursor
    call .roll_screen      ; 滚屏

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 回车和换行处理
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.is_line_feed:             ; 换行符 \n
.is_carriage_return:       ; 回车符 \r
    xor dx, dx
    mov ax, bx
    mov si, 80
    div si                 ; ax = 行号, dx = 列号
    sub bx, dx             ; 移动到行首
    
.is_carriage_return_end:
    add bx, 80             ; 移动到下一行
    cmp bx, 2000
    jl .set_cursor
    
.is_line_feed_end:
    call .roll_screen      ; 滚屏

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 设置光标位置
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.set_cursor:
    ; 设置光标高 8 位
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al
    
    ; 设置光标低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al
    
.put_char_done:
    popad
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 滚屏功能 - 将屏幕内容向上滚动一行
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.roll_screen:
    cld
    ; 将 1-24 行的内容复制到 0-23 行
    mov ecx, 960           ; 24 行 * 80 列 / 2 = 960 次（每次移动 4 字节）
    mov esi, 0xc00b80a0    ; 第 1 行起始地址
    mov edi, 0xc00b8000    ; 第 0 行起始地址
    rep movsd              ; 4 字节 4 字节地复制
    
    ; 清空最后一行（第 24 行）
    mov ebx, 3840          ; 最后一行起始位置（24 * 80 * 2 = 3840）
    mov ecx, 80            ; 80 个字符
.clear_last_line:
    mov word [gs:ebx], 0x0720  ; 0x07 属性 + 0x20 空格
    add ebx, 2
    loop .clear_last_line
    
    mov bx, 1920           ; 光标移到最后一行行首（24 * 80 = 1920）
    ret


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;   ---------          打印字符串                 --------    ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;--------------------------------------------------------------------
;put_str 通过 put_char 来打印以 0 字符结尾的字符串
;--------------------------------------------------------------------
global put_str
;栈中参数是要打印的字符串(字符指针)
put_str:
    ;备份ebx,ecx这两个寄存器
    push ebx
    push ecx
    xor  ecx ,ecx                   ;清0
    mov  ebx ,[esp+12]              ;+8是当前函数栈顶,+12是传入参数的地址(char*)

.goon:
    mov cl ,[ebx]                   ;因为ebx是字符指针首地址,需要通过[]来进行访问
    cmp cl ,0                       ;比较是否为0
    jz .str_over                    ;跳转到字符串结束
    push ecx                        ;压栈
    call put_char                  ;调用打印字符
    add esp ,4                      ;回收栈
    inc ebx                         ;地址++
    jmp .goon                       ;循环

.str_over:
    pop ecx
    pop ebx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; put_int - 打印十进制整数
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 功能：将 32 位整数转换为十进制字符串并打印
; 输入：栈中参数为待打印的数字
; 输出：在屏幕上打印十进制数字
; 说明：采用除 10 取余的方法，从低位到高位逐位转换
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global put_int
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp + 4*9]        ; 获取参数：call 返回地址 4 字节 + pushad 8个寄存器 * 4字节
    mov ebx, put_int_buffer     ; ebx 指向缓冲区
    mov edi, 7                  ; 从缓冲区末尾开始存储（8字节，索引0-7）
    mov byte [ebx + edi], 0     ; 字符串结尾标记
    dec edi                     ; 指向倒数第二个位置
    
    ; 处理负数
    test eax, eax               ; 检查是否为负数
    jns .positive               ; 如果非负，跳转
    
    ; 是负数，先取绝对值
    neg eax                     ; eax = -eax
    mov byte [ebx], '-'         ; 在缓冲区开头放置负号
    jmp .convert_loop
    
.positive:
    mov byte [ebx], ' '         ; 正数开头放空格占位
    
.convert_loop:
    ; 使用除法将数字转换为十进制
    ; eax / 10，商在 eax，余数在 edx
    mov edx, 0                  ; 清空 edx（高32位）
    mov ecx, 10                 ; 除数 10
    div ecx                     ; eax = eax / 10, edx = eax % 10
    
    ; 将余数（0-9）转换为 ASCII 字符
    add dl, '0'                 ; 余数 + '0' = ASCII 字符
    mov [ebx + edi], dl         ; 存入缓冲区（从后往前）
    dec edi                     ; 移动到前一个位置
    
    ; 检查商是否为 0
    test eax, eax               ; eax == 0?
    jnz .convert_loop           ; 不为0，继续循环
    
    ; 现在 edi+1 指向第一个有效数字字符的位置
    ; 如果是负数，edi+1 应该指向第一个数字，负号在 [ebx]
    
.print_result:
    ; 如果是负数，先打印负号
    cmp byte [ebx], '-'
    jne .print_digits
    
    push ebx
    mov cl, '-'
    push ecx
    call put_char
    add esp, 4
    pop ebx
    
.print_digits:
    inc edi                     ; edi 指向第一个有效数字
    
.print_each_digit:
    cmp edi, 7                  ; 是否到达缓冲区末尾（不包括结尾的0）
    jg .print_done
    
    mov cl, [ebx + edi]         ; 获取字符
    cmp cl, 0                   ; 检查是否到达字符串结尾
    je .print_done
    
    push ecx
    call put_char               ; 打印字符
    add esp, 4
    
    inc edi                     ; 下一个字符
    jmp .print_each_digit
    
.print_done:
    popad
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; put_hex - 打印 32 位数的十六进制表示（带 0x 前缀）
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 功能：将 32 位整数转换为十六进制字符串并打印
; 输入：栈中参数为待打印的 32 位数字
; 输出：在屏幕上打印 0x 前缀的十六进制数（如 0x1A2B3C4D）
; 说明：使用除 16 取余的方法，数字 0-9 用 '0'-'9'，10-15 用 'A'-'F'
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global put_hex
put_hex:
    pushad
    mov ebp, esp
    mov eax, [ebp + 4*9]        ; 获取 32 位参数
    mov ebx, put_hex_buffer     ; ebx 指向十六进制缓冲区
    mov edi, 0                  ; edi 记录已存储字符数
    
    ; 特殊处理：如果是 0，直接输出 0x0
    test eax, eax
    jnz .hex_convert_loop
    mov byte [ebx], '0'
    mov edi, 1
    jmp .hex_print_prefix
    
.hex_convert_loop:
    ; 循环除以 16，将余数转换为十六进制字符
    mov edx, 0                  ; 清空 edx（高32位）
    mov ecx, 16                 ; 除数 16
    div ecx                     ; eax = eax / 16, edx = eax % 16
    
    ; 将余数（0-15）转换为十六进制字符
    cmp dl, 9                   ; 判断余数是否小于等于 9
    jle .hex_digit              ; 如果 <= 9，是数字字符
    ; 余数 >= 10，是字母 A-F
    add dl, 'A' - 10            ; 10->A, 11->B, ..., 15->F
    jmp .hex_store
    
.hex_digit:
    add dl, '0'                 ; 0->0, 1->1, ..., 9->9
    
.hex_store:
    mov [ebx + edi], dl         ; 存入缓冲区（从前往后，稍后逆序打印）
    inc edi                     ; 移动到下一个位置
    
    ; 检查商是否为 0
    test eax, eax               ; eax == 0?
    jnz .hex_convert_loop       ; 不为0，继续循环
    
    ; 添加字符串结尾标记
    mov byte [ebx + edi], 0
    
.hex_print_prefix:
    ; 打印 "0x" 前缀
    mov cl, '0'
    push ecx
    call put_char
    add esp, 4
    
    mov cl, 'x'
    push ecx
    call put_char
    add esp, 4
    
    ; 逆序打印缓冲区中的十六进制数字
    dec edi                     ; edi 指向最后一个有效字符
    
.hex_print_loop:
    cmp edi, 0
    jl .hex_done                ; 如果 edi < 0，打印完成
    
    mov cl, [ebx + edi]         ; 获取字符
    push ecx
    call put_char               ; 打印字符
    add esp, 4
    
    dec edi                     ; 移动到前一个字符
    jmp .hex_print_loop
    
.hex_done:
    popad
    ret

;------------------------ set_cursor -----------------------------
; 功能: 设置光标位置
; 参数: cursor_pos (光标位置,0-based)
;-------------------------------------------------------------------
global set_cursor
set_cursor:
    pushad
    mov bx, [esp + 36]      ; 获取参数 cursor_pos
    
    ; 设置光标高 8 位
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al
    
    ; 设置光标低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al
    
    popad
    ret

;------------------------ get_cursor -----------------------------
; 功能: 获取当前光标位置
; 返回: eax = 光标位置
;-------------------------------------------------------------------
global get_cursor
get_cursor:
    push edx
    
    ; 获取光标高 8 位
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    in al, dx
    mov ah, al
    
    ; 获取光标低 8 位
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    in al, dx
    
    ; 返回值在 ax 中,扩展到 eax
    movzx eax, ax
    
    pop edx
    ret
