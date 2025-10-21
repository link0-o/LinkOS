
;主引导程序
;...................................................................................
;初始化段寄存器
section MBR:
    org 0x7c00

    

	mov ax,cs
	mov ds,ax
	mov es,ax
	mov ss,ax
	mov fs,ax
	mov sp,0x7c00
	
    xchg bx,bx            ;断点
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
	;;;	使用10中断，调用13号子功能打印字符串
	mov     ax,0xb800           ;
    mov     ds,ax               ;0xb8000是 显存 文本输出格式 的起始位置.实模式下地址计算是段寄存器*16+偏移量

    mov     byte [ds:0x00],'h'  ;字符
    mov     byte [ds:0x01],0xB4 ;B代表背景青色闪烁,4是字体红色
    
    mov     byte [ds:0x02],'e'
    mov     byte [ds:0x03],0xB4

    mov     byte [ds:0x04],'l'
    mov     byte [ds:0x05],0xB4

    mov     byte [ds:0x06],'l'
    mov     byte [ds:0x07],0xB4

    mov     byte [ds:0x08],'o'
    mov     byte [ds:0x09],0xB4

    mov     byte [ds:0x0A],' '
    mov     byte [ds:0x0B],0xB4

    mov     byte [ds:0x0C],'O'
    mov     byte [ds:0x0D],0xBF;F代表字体是亮白色

    mov     byte [ds:0x0E],'S'
    mov     byte [ds:0x0F],0xBF


;;;;;;;;;;;;;	打印字符串结束    	;;;;;;;;;;;;;;;;
	jmp $						;程序悬停，死循环
	
	times 510-($-$$) db 0		;补齐510个字节
	db 0x55,0xaa            	;BIOS 的最后两个字节的魔数