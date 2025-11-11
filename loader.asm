%include"boot.inc"

;===============================================================================
; LOADER - 加载器程序
; 功能：
;   1. 检测物理内存容量（E820/E801/0x88 三种方法）
;   2. 显示加载信息
;   3. 准备并进入保护模式
;   4. 初始化保护模式环境
;===============================================================================

section loader vstart=LOADER_BASE_ADDR      ; 代码段起始位置 0x900
LOADER_STACK_TOP equ LOADER_BASE_ADDR       ; 栈顶地址（栈向下增长）
jmp loader_start                            ; 跳转到主程序入口

;-------------------------------------------------------------------------------
; 全局描述符表 (GDT - Global Descriptor Table)
;-------------------------------------------------------------------------------
; GDT 是保护模式下的核心数据结构，定义了各个段的属性
; 每个描述符占 8 字节，包含段基址、段界限、访问权限等信息
;-------------------------------------------------------------------------------

    GDT_BASE:   dd  0x00000000
                dd  0x00000000              ; 第 0 个描述符必须为空（CPU 规定）

    CODE_DESC:  dd  0x0000FFFF              ; 段界限低 16 位：0xFFFF
                                            ; 段基址低 16 位：0x0000
                dd  DESC_CODE_HIGH4         ; 段基址高 8 位 + 属性 + 段界限高 4 位
                                            ; G=1(4KB粒度), D=1(32位), L=0, P=1(存在)
                                            ; DPL=0(特权级0), S=1(代码/数据段), TYPE=1000(可执行)
            
    DATA_DESC:  dd  0x0000FFFF              ; 数据段界限：0xFFFF
                dd  DESC_DATA_HIGH4         ; 属性与代码段类似，但 TYPE=0010(可读写)

    VIDEO_DESC: dd  0x80000007              ; 显存段描述符低32位
                                            ; 段界限[15:0] = 0x0007
                                            ; 段基址[15:0] = 0x8000
                dd  DESC_VIDEO_HIGH4        ; 显存段描述符高32位
                                            ; 段基址[23:16] = 0xB8（在字节0位置）
                                            ; 段基址[31:24] = 0x00（在字节3位置）
                                            ; 完整基址：0x000B8000

;-------------------------------------------------------------------------------
; GDT 相关宏定义
;-------------------------------------------------------------------------------
GDT_SIZE    equ $ - GDT_BASE                ; GDT 总大小（字节）
GDT_LIMIT   equ GDT_SIZE - 1                ; GDT 界限（最后一个字节的偏移）

times 60 dq 0    ; 预留 60 个描述符空位（60 × 8 = 480 字节）

;-------------------------------------------------------------------------------
; 内存检测相关变量
;-------------------------------------------------------------------------------
; total_mem_bytes 的内存地址计算：
;   GDT_BASE (0x900) + 3×8 (已定义的描述符) + 60×8 (预留空位) 

;-------------------------------------------------------------------------------
total_mem_bytes dd 0        ; 物理内存总容量（字节）
                            ; 地址：0xB00（供内核引用）

; GDT 指针结构（6 字节）：
;   前 2 字节：GDT 界限
;   后 4 字节：GDT 基地址
gdt_ptr dw GDT_LIMIT
        dd GDT_BASE

; ARDS (Address Range Descriptor Structure) 缓冲区
; 每个 ARDS 结构 20 字节，包含：
;   8 字节：基地址
;   8 字节：长度
;   4 字节：类型（1=可用，2=保留，3=ACPI可回收，4=ACPI NVS，其他=未定义）
; 人工对齐：total_mem_bytes(4) + gdt_ptr(6) + ards_buf(244) + ards_nr(2) = 256 字节
ards_buf times 244 db 0     ; 最多存储 12 个 ARDS 结构（244 / 20 = 12.2）
ards_nr dw 0                ; ARDS 结构体数量

;-------------------------------------------------------------------------------
; 段选择子定义（保护模式）
;-------------------------------------------------------------------------------
; 选择子格式（16 位）：
;   位 15~3：描述符索引
;   位 2：TI（0=GDT, 1=LDT）
;   位 1~0：RPL（请求特权级）
;-------------------------------------------------------------------------------
SELECTOR_CODE   equ (0x0001 << 3) + TI_GDT + RPL0    ; 0x08，代码段选择子
SELECTOR_DATA   equ (0x0002 << 3) + TI_GDT + RPL0    ; 0x10，数据段选择子
SELECTOR_VIDEO  equ (0x0003 << 3) + TI_GDT + RPL0    ; 0x18，显存段选择子

loadermsg db 'hello loader!'              ; 加载器提示信息

;===============================================================================
; 主程序入口
;===============================================================================
loader_start:

;===============================================================================
; 第 1 步：检测物理内存容量
;===============================================================================
; 采用三种方法，优先级从高到低：
;   1. INT 15h, EAX=0xE820 - 最详细，返回内存布局
;   2. INT 15h, AX=0xE801  - 简单，最大支持 4GB
;   3. INT 15h, AH=0x88    - 最老，最大支持 64MB
;===============================================================================

;-------------------------------------------------------------------------------
; 方法 1：INT 15h, EAX=0xE820 - 获取完整内存布局
;-------------------------------------------------------------------------------
; 功能：循环调用 INT 15h 获取所有 ARDS 结构体
; 输入：
;   EAX = 0xE820           功能号
;   EBX = 0（首次调用）    后续调用时为上次返回值
;   ECX = 20               缓冲区大小（ARDS 结构体大小）
;   EDX = 'SMAP'           签名（0x534D4150）
;   ES:DI                  指向 ARDS 缓冲区
; 输出：
;   CF = 0                 成功
;   EAX = 'SMAP'           签名验证
;   ECX = 实际写入字节数
;   EBX = 0                最后一个 ARDS（结束标志）
;         非0              还有更多 ARDS（继续调用）
;-------------------------------------------------------------------------------
    xor ebx, ebx                ; 第一次调用时 EBX 必须为 0
    mov edx, 0x534d4150         ; 'SMAP' 签名，只需设置一次
    mov di, ards_buf            ; ES:DI 指向 ARDS 缓冲区
    
.e820_mem_get_loop:             ; 循环获取每个 ARDS 结构体
    mov eax, 0x0000e820         ; 功能号（每次调用前需重新设置）
    mov ecx, 20                 ; ARDS 结构体大小
    int 0x15                    ; 调用 BIOS 中断
    jc .e820_failed_so_try_e801 ; CF=1 表示出错，尝试方法 2
    add di, cx                  ; DI 指向下一个 ARDS 位置
    inc word [ards_nr]          ; ARDS 计数器加 1
    cmp ebx, 0                  ; EBX=0 表示已是最后一个
    jnz .e820_mem_get_loop      ; 否则继续获取

;-------------------------------------------------------------------------------
; 遍历所有 ARDS，找出最大内存结束地址
;-------------------------------------------------------------------------------
; 算法：
;   遍历每个 ARDS，计算 base_addr + length，找出最大值
;   最大的结束地址 = 物理内存总容量
;-------------------------------------------------------------------------------
    mov cx, [ards_nr]           ; 循环次数 = ARDS 数量
    mov ebx, ards_buf           ; EBX 指向第一个 ARDS
    xor edx, edx                ; EDX 存储最大结束地址，初始化为 0
.find_max_mem_area:             ; 无需判断 type，最大块一定是可用内存
    mov eax, [ebx]              ; EAX = base_addr_low
    add eax, [ebx+8]            ; EAX += length_low（计算结束地址）
    add ebx, 20                 ; EBX 指向下一个 ARDS（20 字节）
    cmp edx, eax                ; 比较当前最大值和新计算的值
    jge .next_ards              ; 如果 EDX >= EAX，跳过
    mov edx, eax                ; 否则更新最大值
.next_ards:
    loop .find_max_mem_area     ; 循环
    jmp .mem_get_ok             ; 跳转到成功处理

;-------------------------------------------------------------------------------
; 方法 2：INT 15h, AX=0xE801 - 获取内存大小（最大 4GB）
;-------------------------------------------------------------------------------
; 功能：获取内存容量，分两部分返回
; 输入：
;   AX = 0xE801            功能号
; 输出：
;   CF = 0                 成功
;   AX = CX                低 15MB 内存容量（KB）
;   BX = DX                16MB~4GB 内存容量（64KB 单位）
;-------------------------------------------------------------------------------
.e820_failed_so_try_e801:
    mov ax,0xe801
    int 0x15
    jc .e801_failed_so_try88    ; 失败则尝试方法 3

; 第 1 部分：计算低 15MB 内存（AX 单位为 KB）
    mov cx,0x400                ; 1KB = 1024 字节
    mul cx                      ; AX * 1024，结果：DX:AX（32 位）
    shl edx,16                  ; EDX 高 16 位 = DX
    and eax,0x0000FFFF          ; EAX 低 16 位 = AX
    or edx,eax                  ; 组合成完整的 32 位值
    add edx, 0x100000           ; 加上第 1MB（BIOS 保留）
    mov esi,edx                 ; 暂存到 ESI

; 第 2 部分：计算 16MB 以上内存（BX 单位为 64KB）
    xor eax,eax
    mov ax,bx                   ; AX = BX
    mov ecx, 0x10000            ; 64KB = 65536 字节
    mul ecx                     ; EAX * 64KB，结果：EDX:EAX（64 位）
    add esi,eax                 ; 只加低 32 位（4GB 内 EDX 必为 0）
    mov edx,esi                 ; 总内存 = 低 15MB + 高部分
    jmp .mem_get_ok

;-------------------------------------------------------------------------------
; 方法 3：INT 15h, AH=0x88 - 获取内存大小（最大 64MB）
;-------------------------------------------------------------------------------
; 功能：获取扩展内存容量（1MB 以上）
; 输入：
;   AH = 0x88              功能号
; 输出：
;   CF = 0                 成功
;   AX                     扩展内存容量（KB）
;-------------------------------------------------------------------------------
.e801_failed_so_try88:
    mov ah, 0x88
    int 0x15
    jc .error_hlt_real          ; 所有方法都失败，系统挂起
    and eax,0x0000FFFF          ; 只保留 AX（16 位）

    ; 转换为字节：AX(KB) * 1024
    mov cx, 0x400               ; 1KB = 1024
    mul cx                      ; 16 位乘法：AX * CX → DX:AX
    shl edx, 16                 ; EDX 高 16 位 = DX
    or edx, eax                 ; EDX = DX:AX（32 位）
    add edx,0x100000            ; 加上第 1MB


.mem_get_ok:
    mov [total_mem_bytes], edx  ; 将内存换为 byte 单位后存入 total_mem_bytes 处
    jmp .continue_boot          ; 跳过错误处理

.error_hlt_real:                ; 实模式下的错误处理
    hlt

.continue_boot:                 ; 继续启动流程

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;--------   打印字符串  ----------;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ; 设置栈指针
    mov sp, LOADER_BASE_ADDR

    mov bp, loadermsg          ; ES:BP -> 字符串地址
    mov cx, 13                 ; 字符串长度
    mov ax, 0x1301             ; 功能号：AH=13h, AL=01h（显示字符串并移动光标）
    mov bx, 0x0053             ; BH=0（页号），BL=0x53 → 粉红底 + 青色字
    mov dx, 0x1800             ; DH=24行, DL=0列
    int 0x10                   ; 调用BIOS视频中断

;---------- 准备进入保护模式 ----------
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;1 打开 A20
;2 加载 gdt
;3 将 cr0 的 pe 位置 1

;----------------- 打开 A20 地址线 ----------------
; A20 地址线控制方法：通过 0x92 端口（快速门）
; bit 0: 0=快速复位, 1=正常运行
; bit 1: 0=禁用A20, 1=启用A20
; 历史原因：8086 只有 20 根地址线，访问超过 1MB 的地址会回绕
; 80286+ 需要显式打开 A20 才能访问 1MB 以上的内存
;---------------------------------------------------
    in  al,0x92               ; 读取快速 A20 端口当前值
    or  al,0000_0010B         ; 设置 bit 1 = 1（启用 A20）
    out 0x92,al               ; 写回端口

;----------------- 加载 GDT ----------------
; LGDT 指令：加载全局描述符表寄存器 (GDTR)
; gdt_ptr 是一个 6 字节结构：
;   字节 0-1: GDT 界限 (总字节数 - 1)
;   字节 2-5: GDT 基址
; 加载后 CPU 可通过段选择子访问 GDT 中的段描述符
;--------------------------------------------
    lgdt [gdt_ptr]            ; 将 GDT 指针加载到 GDTR 寄存器

;----------------- cr0 第 0 位置 1 ----------------
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start    ; 刷新流水线

[bits 32]
p_mode_start:
    ;------- 初始化数据段寄存器 -------
    ; 在保护模式下，段寄存器存放的是段选择子（Segment Selector）
    ; 段选择子结构（16位）：
    ;   bit 15-3: 索引（Index）- 指向 GDT 中的描述符
    ;   bit 2:    TI (Table Indicator) - 0=GDT, 1=LDT
    ;   bit 1-0:  RPL (Requested Privilege Level) - 特权级
    
    mov ax, SELECTOR_DATA      ; SELECTOR_DATA 指向 GDT 中的数据段描述符
    mov ds, ax                 ; DS = 数据段选择子
    mov es, ax                 ; ES = 数据段选择子
    mov ss, ax                 ; SS = 栈段选择子
    mov esp, LOADER_STACK_TOP  ; ESP = 栈顶地址（栈从高地址向低地址增长）

    ;------- 初始化显存段寄存器 -------
    ; GS 专门用于访问显存（0xB8000）
    mov ax, SELECTOR_VIDEO     
    mov gs, ax                 

    ; 在屏幕第二行第一列显示 'P'（表示 Protected Mode）
    ; 显存格式：每个字符2字节（字符+属性）
    ; 第二行偏移 = 80 * 2 = 160
    mov byte [gs:160], 'P'     ; 字符：'P'
    mov byte [gs:161], 0x0A    ; 属性：黑底亮绿字（0x0A）

;===============================================================================
; 加载内核到内存（保护模式下）
;===============================================================================
; 在保护模式下从硬盘读取 kernel.bin 到物理地址 KERNEL_BIN_BASE_ADDR (0x70000)
; 读取 200 个扇区（100KB），足够容纳当前内核
; 注意：如果内核 > 192KB，需要将 KERNEL_BIN_BASE_ADDR 改到 1MB 以上
;===============================================================================

    mov eax, KERNEL_START_SECTOR    ; kernel.bin 所在的起始扇区号
    mov ebx, KERNEL_BIN_BASE_ADDR   ; 目标物理地址（0x70000）
    mov ecx, 200                    ; 读取 200 扇区 = 100KB
    call rd_disk_m_32               ; 调用保护模式磁盘读取函数

    mov byte [gs:162], 'K'          ; 显示 'K' 表示内核加载完成
    mov byte [gs:163], 0x0C         ; 红色


; ===================================================================================
;          OS 内核初始化：开启分页机制 & 迁移 GDT 至高地址空间
;      功能：设置页表、重定位 GDT 和显存段、开启分页、测试虚拟内存访问
; ===================================================================================

; 此函数已完成以下工作：
;   - 在 PAGE_DIR_TABLE_POS 处建立页目录
;   - 建立多个页表（如映射低 1MB 内存）
;   - 初始化页分配位图（用于后续动态分配）
    call set_page       



    sgdt [gdt_ptr]              ;导出 当前 GDT 信息

;将 gdt 描述符中视频段描述符中的段基址+0xc0000000
    mov ebx, [gdt_ptr + 2]                          ; ebx = 当前 GDT 的物理基地址（从 gdt_ptr.base 取出）
                                                    ; +2 是因为前 2 字节是 limi
    or dword [ebx + 0x18 + 4], 0xc0000000           ; 视频段是第 3 个段描述符，每个描述符是 8 字节，故 0x18
                                                    ; 结果：原显存地址 0xB8000 → 新地址 0xC00B8000

    add dword [gdt_ptr + 2], 0xc0000000             ; 把 gdt_ptr 中记录的 GDT 基地址加上 0xC0000000
                                                    ; 即：告诉 CPU “新的 GDT 在 0xC0xxxxxx”

    add esp, 0xc0000000             ; 将当前栈指针从低地址（0x900 -> 0x0000900）调整为高地址

    mov eax, PAGE_DIR_TABLE_POS     ; eax = 页目录的物理地址（ 0x101000）
    mov cr3, eax                    ; 将页目录地址写入 cr3 寄存器
                                    ; MMU 将从此处查找页表结构

    mov eax, cr0                    ; 读取 cr0 控制寄存器
    or eax, 0x80000000              ; 设置 PG 位（第 31 位）为 1 → 启用分页
    mov cr0, eax                    ; 写回 cr0

;在开启分页后，用 gdt 新的地址重新加载
    lgdt [gdt_ptr]                  ; 重新加载 GDT

;重新加载段寄存器，使新的段描述符生效
    jmp SELECTOR_CODE:reload_segments   ; 刷新 CS 段寄存器

reload_segments:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    
    mov ax, SELECTOR_VIDEO          ; 重新加载 GS，使用新的显存段基址（0xC00B8000）
    mov gs, ax

    mov byte [gs:160], 'V'          ; 视频段段基址已经被更新，用字符 V 表示 virtual addr
    mov byte [gs:161], 0x0A         ; 绿色

;===============================================================================
; 初始化内核：解析 ELF 格式并加载到正确的虚拟地址
;===============================================================================
    call kernel_init
    mov esp, 0xc009f000             ;设置栈顶
    
    mov byte [gs:164], 'I'          ; 显示 'I' 表示内核初始化完成
    mov byte [gs:165], 0x0E         ; 黄色

;===============================================================================
; 跳转到内核入口点
;===============================================================================
    jmp KERNEL_ENTRY_POINT          ; 跳转到内核 main 函数

;悬停（如果内核返回，则在此死循环）
    jmp $

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;------------- 创建页目录及页表 ---------------;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;清除页表目录的地址空间
set_page:
    mov ecx, 4096           ;4KB=4096B
    mov esi, 0
;使用循环清除
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0          ;PAGE_DIR_TABLE_POS equ 0x100000,模仿linux,高地址1G分配给系统内核
    inc esi                 ;自增
    loop .clear_page_dir    ;ecx计数-1,然后循环跳转

;创建页目录(PDE)
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000                     ;此时eax为第一个页表的位置和属性(因为页目录本身也占用1KB)
    mov ebx, eax                        ;为.create_pte 做准备,ebx作为基址
    
; 建立虚拟地址的双重映射：将低地址空间与内核空间的起始部分指向同一物理内存
; 目的是让内核在高地址运行时，仍能访问低地址的数据（如引导信息、GDT等）

    or eax, PG_US_U | PG_RW_W | PG_P
; 设置页目录项属性：
; - PG_P (Present):     1 → 页面存在
; - PG_RW_W (Writable): 1 → 可读写
; - PG_US_U (User):     1 → 用户和内核均可访问（所有特权级）
; 此时 eax = 第一个页表的物理地址（如 0x101000） + 属性位（0x7）

    mov [PAGE_DIR_TABLE_POS + 0x0], eax
; 将第一个页目录项（PDE[0]）设置为指向第一个页表
; 负责映射虚拟地址 0x00000000 ~ 0x003FFFFF（即前 4MB 的用户空间）

    mov [PAGE_DIR_TABLE_POS + 0xc00], eax
; 将第 768 个页目录项（PDE[768]）设置为相同的值
; 因为 0xC00 = 768 × 4，对应虚拟地址 0xC0000000 开始的 4MB 区域
; 即：虚拟地址 0xC0000000 ~ 0xC03FFFFF 也被映射到同一个页表
;
; 实现效果：低地址（0x0）和内核空间起始（0xC0000000）共享相同物理内存
;       为内核加载到高地址后访问低地址内容做准备

; 虚拟地址空间划分说明（标准 3GB/1GB 模型）：
;   0x00000000 ~ 0xBFFFFFFF : 用户空间（共 3GB）
;   0xC0000000 ~ 0xFFFFFFFF : 内核空间（共 1GB）
; 当前仅初始化了 PDE[0] 和 PDE[768]，后续需填充其他项以扩展映射

    sub eax, 0x1000
; 恢复得到页目录表自身的物理地址（原为 0x101000，减去 0x1000 → 0x100000）

    mov [PAGE_DIR_TABLE_POS + 4092], eax
; 将最后一个页目录项（PDE[1023]）指向页目录表自己（自映射）
; 4092 = 1023 × 4，是页目录中最后一项的偏移地址
;
; 作用：实现“页目录自映射”
;       之后可通过虚拟地址 0xFFC00000 + (index << 12) 访问任意 PDE
;       极大简化后期动态管理页表的操作


;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;-------下面创建页表项--------;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; 为第一个页表（0x101000）创建页表项，映射低端 1MB 内存（0x0～0xFFFFF）
    mov ecx, 256                                ; 1M 低端内存 / 每页大小 4k = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P           ; 属性为 7，US=1，RW=1，P=1

.create_pte:
    mov [ebx+esi*4], edx                        ; ebx=0x101000，第一个页表的地址
    add edx, 4096                               ; 物理地址 += 4KB
    inc esi
    loop .create_pte

; 创建内核页表的 PDE（页目录项 768，对应虚拟地址 0xC0000000）
; 第 768 个 PDE 对应虚拟地址 768*4MB = 0xC0000000（3GB）
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000                             ; 第二个页表的位置（0x102000）
    or  eax, PG_US_U | PG_RW_W | PG_P           ; 页目录项属性 US=1, RW=1, P=1
    mov ebx, PAGE_DIR_TABLE_POS
    mov [ebx+768*4], eax                        ; 在页目录第 768 项写入第二个页表地址

; 为第二个页表（0x102000）创建页表项，映射内核虚拟地址
; 虚拟地址 0xC0000000 映射到物理地址 0x0
; 这样内核在 0xC0000000+X 的虚拟地址实际访问物理地址 0x0+X
    mov ebx, PAGE_DIR_TABLE_POS
    add ebx, 0x2000                             ; ebx = 0x102000，第二个页表地址
    mov ecx, 256                                ; 创建 256 个页表项，映射 1MB
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P           ; 属性为 7

.create_kernel_pte:
    mov [ebx+esi*4], edx
    add edx, 4096
    inc esi
    loop .create_kernel_pte

    ret

;===============================================================================
; rd_disk_m_32 - 保护模式下读取硬盘扇区
;===============================================================================
; 功能：在 32 位保护模式下从硬盘读取 n 个扇区到指定内存地址
; 输入：
;   eax = LBA 起始扇区号
;   ebx = 目标内存地址（物理地址）
;   ecx = 要读取的扇区数
; 输出：无
; 说明：使用 PIO 模式（Port I/O）与 ATA 硬盘控制器通信
;===============================================================================
rd_disk_m_32:
    push eax
    push ebx
    push ecx
    push edx
    push esi

    mov esi, eax                    ; 备份 LBA 扇区号到 esi
    mov edi, ecx                    ; 备份扇区数到 edi

    ;--- 1. 设置要读取的扇区数 ---
    mov dx, 0x1f2                   ; 端口 0x1f2：扇区数寄存器
    mov al, cl                      ; 要读取的扇区数
    out dx, al

    mov eax, esi                    ; 恢复 LBA 扇区号

    ;--- 2. 将 LBA 地址写入端口 0x1f3~0x1f6 ---
    ; 2.1 写入 LBA 低 8 位
    mov dx, 0x1f3
    out dx, al

    ; 2.2 写入 LBA 中 8 位（位 8-15）
    shr eax, 8
    mov dx, 0x1f4
    out dx, al

    ; 2.3 写入 LBA 高 8 位（位 16-23）
    shr eax, 8
    mov dx, 0x1f5
    out dx, al

    ; 2.4 写入设备寄存器（位 24-27 + LBA 模式标志）
    shr eax, 8
    and al, 0x0f                    ; 只保留低 4 位（LBA 的位 24-27）
    or al, 0xe0                     ; 设置位 5-7 为 111（LBA 模式，主盘）
    mov dx, 0x1f6
    out dx, al

    ;--- 3. 发送读取命令 ---
    mov dx, 0x1f7                   ; 端口 0x1f7：命令/状态寄存器
    mov al, 0x20                    ; 命令 0x20：读取扇区
    out dx, al

    ;--- 4. 检测硬盘状态，等待数据准备好 ---
.not_ready:
    nop                             ; 短暂延迟
    in al, dx                       ; 读取状态寄存器
    and al, 0x88                    ; 检查 BSY(位7) 和 DRQ(位3)
    cmp al, 0x08                    ; DRQ=1 且 BSY=0 表示数据就绪
    jnz .not_ready                  ; 未就绪则继续等待

    ;--- 5. 从数据端口读取数据 ---
    mov ax, di                      ; 扇区数
    mov dx, 256                     ; 每个扇区 512 字节 = 256 个字（word）
    mul dx                          ; ax = 扇区数 × 256
    mov ecx, eax                    ; ecx = 总共要读取的字数

    mov dx, 0x1f0                   ; 端口 0x1f0：数据寄存器
.go_on_read:
    in ax, dx                       ; 读取 2 字节
    mov [ebx], ax                   ; 写入目标内存
    add ebx, 2                      ; 地址 +2
    loop .go_on_read                ; 循环读取

    pop esi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

;===============================================================================
; kernel_init - 初始化内核（解析 ELF 并加载到虚拟地址）
;===============================================================================
; 功能：解析 kernel.bin（ELF 格式），将各个 segment 拷贝到编译时指定的虚拟地址
; 输入：kernel.bin 已加载到 KERNEL_BIN_BASE_ADDR (0x70000)
; 输出：内核各段被正确加载到虚拟地址空间
; 说明：
;   - 读取 ELF header 获取 program header table 信息
;   - 遍历每个 program header，将 PT_LOAD 类型的段拷贝到目标虚拟地址
;===============================================================================
kernel_init:
    xor eax, eax
    xor ebx, ebx                    ; ebx 记录 program header table 地址
    xor ecx, ecx                    ; cx 记录 program header 数量
    xor edx, edx                    ; dx 记录 program header 大小

    ; 读取 ELF header 中的关键字段
    mov dx, [KERNEL_BIN_BASE_ADDR + 42]   ; e_phentsize (program header 大小)
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]  ; e_phoff (第1个 program header 的偏移)
    add ebx, KERNEL_BIN_BASE_ADDR         ; 转换为物理地址
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]   ; e_phnum (program header 数量)

.each_segment:
    cmp byte [ebx + 0], PT_NULL     ; p_type 字段：检查是否为 PT_NULL
    je .PT_NULL                      ; 如果是，跳过此 program header

    ; 检查虚拟地址是否在内核空间（0xC0000000 以上）
    ; 过滤掉低地址段（如 0x08048000 的 .note 段）
    mov eax, [ebx + 8]              ; eax = p_vaddr
    cmp eax, 0xC0000000             ; 检查是否 >= 3GB（内核虚拟地址空间）
    jb .PT_NULL                     ; 如果小于，跳过此段

    ; 准备调用 mem_cpy(dst, src, size)
    ; 参数从右往左压栈

    push dword [ebx + 16]           ; 参数3：size = p_filesz（段在文件中的大小）
    
    mov eax, [ebx + 4]              ; p_offset（段在文件中的偏移）
    add eax, KERNEL_BIN_BASE_ADDR   ; 转换为物理地址
    push eax                        ; 参数2：src（源地址）
    
    push dword [ebx + 8]            ; 参数1：dst = p_vaddr（目标虚拟地址）
    
    call mem_cpy                    ; 执行内存拷贝
    add esp, 12                     ; 清理栈中的 3 个参数

.PT_NULL:
    add ebx, edx                    ; ebx 指向下一个 program header
    loop .each_segment              ; 循环处理所有 program header
    ret

;===============================================================================
; mem_cpy - 内存拷贝函数
;===============================================================================
; 功能：逐字节拷贝内存
; 输入：栈中三个参数
;   [ebp + 8]  = dst（目标地址）
;   [ebp + 12] = src（源地址）
;   [ebp + 16] = size（字节数）
; 输出：无
; 说明：使用 rep movsb 指令实现高效拷贝
;===============================================================================
mem_cpy:
    cld                             ; 清除方向标志，使 esi/edi 自增
    push ebp
    mov ebp, esp
    push ecx                        ; 保存 ecx（外层可能在使用）
    push esi
    push edi
    
    mov edi, [ebp + 8]              ; 目标地址
    mov esi, [ebp + 12]             ; 源地址
    mov ecx, [ebp + 16]             ; 拷贝字节数
    rep movsb                       ; 逐字节拷贝：*(edi++) = *(esi++)，重复 ecx 次
    
    ; 恢复环境
    pop edi
    pop esi
    pop ecx
    pop ebp
    ret