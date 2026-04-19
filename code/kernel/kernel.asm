[bits 32]
%define SELECTOR_K_CODE 0x08        ; ((1 << 3) + (TI_GDT << 2) + RPL0)
%define SELECTOR_K_DATA 0x10
%define SELECTOR_U_CODE 0x2B
%define SELECTOR_U_DATA 0x33
;--------------------------------------------------
; 中断入口表
; 为 33 个中断/异常建立统一处理程序
;--------------------------------------------------

%define ERROR_CODE nop       ;CPU 已压入错误码，不做操作
%define ZERO push 0          ;CPU 没压入错误码，手工压入 0

extern idt_table             ;C 中注册的中断处理程序数组

section .data
global intr_entry_table
intr_entry_table:

;--------------------------------------------------
; VECTOR 宏 - 批量生成中断处理程序入口
; 参数：%1=中断号  %2=ERROR_CODE/ZERO
; 功能：保存上下文 -> 调用 C 处理函数 -> 恢复上下文
;--------------------------------------------------
%macro VECTOR 2
section .text
intr%1entry:
    %2                      ; 若 CPU 没压入错误码，手工压入 0 占位
    push ds
    push es
    push fs
    push gs
    pushad                  ; 保存 8 个通用寄存器
    
    ; 发送 EOI (中断结束) 到 8259A
    mov al, 0x20
    out 0xa0, al            ; 从片
    out 0x20, al            ; 主片
    
    ; 调用 C 层的中断处理函数
    push %1                 ; 压入中断向量号作为参数
    call [idt_table + %1*4] ; 调用 idt_table[vec_nr]
    jmp intr_exit           ; 跳转到统一的退出代码
    
section .data
    dd intr%1entry          ; 存储中断入口地址
%endmacro

;--------------------------------------------------
; 0x00-0x1F: CPU 异常
; 0x20-0x2F: 硬件中断 (8259A)
;--------------------------------------------------

VECTOR 0x00, ZERO            ;除法错误
VECTOR 0x01, ZERO            ;调试
VECTOR 0x02, ZERO            ;不可屏蔽中断
VECTOR 0x03, ZERO            ;断点
VECTOR 0x04, ZERO            ;溢出
VECTOR 0x05, ZERO            ;边界检查
VECTOR 0x06, ZERO            ;无效操作码
VECTOR 0x07, ZERO            ;设备不可用
VECTOR 0x08, ERROR_CODE      ;双重故障
VECTOR 0x09, ZERO            ;协处理器段超限
VECTOR 0x0a, ERROR_CODE      ;无效 TSS
VECTOR 0x0b, ERROR_CODE      ;段不存在
VECTOR 0x0c, ERROR_CODE      ;栈段错误
VECTOR 0x0d, ERROR_CODE      ;一般保护错误
VECTOR 0x0e, ERROR_CODE      ;页错误
VECTOR 0x0f, ZERO
VECTOR 0x10, ZERO            ;浮点错误
VECTOR 0x11, ERROR_CODE      ;对齐检查
VECTOR 0x12, ZERO            ;机器检查
VECTOR 0x13, ZERO            ;SIMD 浮点异常
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO
VECTOR 0x18, ZERO
VECTOR 0x19, ZERO
VECTOR 0x1a, ZERO
VECTOR 0x1b, ZERO
VECTOR 0x1c, ZERO
VECTOR 0x1d, ZERO
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO

VECTOR 0x20, ZERO            ;时钟
VECTOR 0x21, ZERO            ;键盘
VECTOR 0x22, ZERO            ;级联
VECTOR 0x23, ZERO            ;串口 2
VECTOR 0x24, ZERO            ;串口 1
VECTOR 0x25, ZERO            ;并口 2
VECTOR 0x26, ZERO            ;软盘
VECTOR 0x27, ZERO            ;并口 1
VECTOR 0x28, ZERO            ;实时时钟
VECTOR 0x29, ZERO            ;重定向
VECTOR 0x2a, ZERO
VECTOR 0x2b, ZERO
VECTOR 0x2c, ZERO            ;PS/2 鼠标
VECTOR 0x2d, ZERO            ;协处理器
VECTOR 0x2e, ZERO            ;IDE 主盘
VECTOR 0x2f, ZERO            ;IDE 从盘

;--------------------------------------------------
; 中断退出统一处理
;--------------------------------------------------
section .text
global intr_exit
intr_exit:
    add esp, 4              ; 跳过中断向量号参数（vec_no）
    popad                   ; 恢复通用寄存器
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4              ; 跳过 err_code
    iretd                   ; 中断返回

;--------------------------------------------------
; 系统调用入口（int 0x80）
;--------------------------------------------------
extern syscall_table

global syscall_handler
syscall_handler:
    ; 保存上下文
    push 0                  ; 压入错误码占位（系统调用没有错误码）
    
    push ds
    push es
    push fs
    push gs
    pushad                  ; 保存所有通用寄存器
    
    push 0x80               ; 压入中断向量号 0x80
    
    ; 切换到内核数据段
    push edx                ; 保存 edx（系统调用可能需要）
    mov dx, SELECTOR_K_DATA
    mov ds, dx
    mov es, dx
    pop edx                 ; 恢复 edx
    
    ; 调用 C 函数处理系统调用
    ; syscall_table 是一个函数指针数组
    ; eax 中是系统调用号，ebx/ecx/edx/esi/edi 中是参数
    
    push edi                ; 第 5 个参数
    push esi                ; 第 4 个参数
    push edx                ; 第 3 个参数
    push ecx                ; 第 2 个参数
    push ebx                ; 第 1 个参数
    
    call [syscall_table + eax*4]  ; 调用对应的系统调用处理函数
    add esp, 20             ; 清理 5 个参数（5*4=20 字节）
    
    ; 将返回值存入栈中的 eax 位置（popad 会恢复它）
    mov [esp + 8*4], eax    ; 跳过 8 个寄存器到达 eax 的位置
    
    jmp intr_exit           ; 通过 intr_exit 返回用户态
