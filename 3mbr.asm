%include "boot.inc"
;主引导程序
;...................................................................................
;初始化段寄存器
SECTION MBR vstart=0x7c00

	mov ax,cs
	mov ds,ax
	mov es,ax
	mov ss,ax
	mov fs,ax
	mov sp,0x7c00
	
    xchg bx,bx         ;魔术断点
;清屏功能，利用0x06号功能，上卷全部行
;-----------------------------------------------------------------------------------
;INT 0x10 功能好；0x06	功能描述：上卷窗口
;-----------------------------------------------------------------------------------
;输入：
;AH 功能号=0x06，ax寄存器的高八位
;AL = 上卷行数（如果为0，表示全部） ，ax寄存器的低八位
;BH = 上卷行属性
;(CL,CH) = 窗口左上角的（X，Y）位置
;(DL,DH) =窗口右下角的（X，Y）的位置
;无返回值
    
	mov		ax,0x600
	mov		bx,0x700
	mov		cx,0
	mov		dx,0x184f
						;VGA 文本模式中,一行只能容纳 80 个字符,共 25 行｡
	int		0x10		;INT 0x10

;;;;;;;;;	打印字符串	;;;;;;;;;;
	mov     ax,0xb800           ;
    mov     gs,ax               ;0xb8000是 显存 文本输出格式 的起始位置.实模式下地址计算是段寄存器*16+偏移量

    mov     byte [gs:0x00],'h'  ;字符
    mov     byte [gs:0x01],0xB4 ;B代表背景青色闪烁,4是字体红色
    
    mov     byte [gs:0x02],'e'
    mov     byte [gs:0x03],0xB4

    mov     byte [gs:0x04],'l'
    mov     byte [gs:0x05],0xB4

    mov     byte [gs:0x06],'l'
    mov     byte [gs:0x07],0xB4

    mov     byte [gs:0x08],'o'
    mov     byte [gs:0x09],0xB4

    mov     byte [gs:0x0A],' '
    mov     byte [gs:0x0B],0xB4

    mov     byte [gs:0x0C],'O'
    mov     byte [gs:0x0D],0xBF;F代表字体是亮白色

    mov     byte [gs:0x0E],'S'
    mov     byte [gs:0x0F],0xBF


;;;;;;;;;;;;;	打印字符串结束    	;;;;;;;;;;;;;;;;
	mov eax,LOADER_START_SECTOR     ;起始扇区lba地址
    mov bx,LOADER_BASE_ADDR         ;写入的地址
    mov cx,4                    ;写入的扇区个数

    call rd_disk           ;函数调用
    jmp LOADER_BASE_ADDR        ;跳转

;.....................................................
;.....................................................
;函数功能:读取硬盘的n扇区
rd_disk:
                                ;eax是lba的扇区号
                                ;bx是写入的内存地址
    mov esi,eax;    ;备份eax寄存器,因为该该端口号只接受ax,al,ah,eax寄存器
    mov di,cx

;实现读写功能
;1.设置读的扇区数
    mov dx,0x1f2        ;0x1f2端口号,设置扇区数
    mov al,cl
    out dx,al           ;读取的扇区数

    mov eax,esi         ;恢复寄存器
;2.将lba地址写入端口号0x1f3~0x1f6中
    mov dx,0x1f3        ;lba的低8位
    out dx,al

    shr eax,8           ;逻辑右移8位
    mov dx,0x1f4         
    out dx,al           ;依旧取eax的低8位,原本eax的中8位,写入lba的中8位(端口号0x1f4)

    shr eax,8
    mov dx,0x1f5
    out dx,al           ;依旧取eax的低8位,写入lba的高8位(端口号0x1f5)

    shr eax,8
    and al,0x0f         ;lba的24-27的位
    or al,0xe0          ;设置4-7位为1110,表示当前处于lba模式
    mov dx,0x1f6        ;
    out dx,al           ;写入0x1f6端口号(Device寄存器)

;3.向0x1f7写入 读取的命令:0x20
    mov dx,0x1f7
    mov al,0x20
    out dx,al

;4.检查硬盘状态
    .not_ready:
    nop
    in al,dx
    and al,0x88
    cmp al,0x08
    jnz .not_ready

    test al,0x08     ; 检查第4位（DRQ），硬盘是否准备好数据传输
    jz .not_ready    ; 如果未准备好，则继续等待

;5.从0x1f0端口读入数据
    mov ax,di
    mov dx,256
    mul dx                  ;di位要读入的扇区数,每个扇区512个字节,由于每次读取两个字节,需要di*256次
    mov cx,ax           
    mov dx,0x1f0
.go_on_read:
    in ax,dx
    mov [bx],ax
    add bx,2
    loop .go_on_read
    ret
	
	times 510-($-$$) db 0		;补齐510个字节
	db 0x55,0xaa            	;BIOS 的最后两个字节的魔数