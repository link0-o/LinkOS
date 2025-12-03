[bits 32]
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
intr_exit:
    add esp, 4              ; 跳过中断向量号参数
    popad                   ; 恢复通用寄存器
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4              ; 跳过 error_code
    iret                    ; 中断返回
