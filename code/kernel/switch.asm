[section .text]
[bits 32]

; switch_to(struct task_struct* prev, struct task_struct* next)
; 功能：保存当前线程的寄存器环境，恢复下一个线程的寄存器环境
; 参数：
;   prev: 当前线程的 PCB 指针 (在栈中 [esp+4])
;   next: 下一个线程的 PCB 指针 (在栈中 [esp+8])
global switch_to
switch_to:
    ; 保存 prev 线程的寄存器环境
    ; 按照 struct thread_stack 的顺序：esi, edi, ebx, ebp
    push esi
    push edi
    push ebx
    push ebp
    
    ; 获取 prev 的 PCB 指针
    mov eax, [esp + 20]         ; prev = [esp + 4 + 16]，因为已经压入了 4 个寄存器(16字节)
    
    ; 保存当前栈顶到 prev->self_kstack
    ; self_kstack 是 task_struct 的第一个成员，偏移为 0
    mov [eax], esp              ; prev->self_kstack = esp
    
    ; ==================== 上面是保存旧线程，下面是恢复新线程 ====================
    
    ; 获取 next 的 PCB 指针
    mov eax, [esp + 24]         ; next = [esp + 8 + 16]
    
    ; 恢复 next 线程的栈指针
    mov esp, [eax]              ; esp = next->self_kstack
    
    ; 从栈中恢复 next 线程的寄存器环境
    ; 按照 struct thread_stack 的顺序（逆序弹出）：ebp, ebx, edi, esi
    pop ebp
    pop ebx
    pop edi
    pop esi
    
    ; 此时栈顶是 eip（返回地址）
    ; 对于第一次运行的线程，eip 指向 kernel_thread
    ; 对于已经运行过的线程，eip 指向上次 switch_to 之后的返回地址
    ret                         ; 弹出栈顶作为返回地址，跳转到 eip 指向的地址
