# 操作系统启动代码文档

## 📁 项目结构

```
汇编/
├── include/
│   └── boot.inc         # 公共宏定义和常量
├── 3mbr.asm             # 主引导记录 (MBR)
├── loader.asm           # 加载器程序
├── mbr.bin              # MBR 编译产物 (512字节)
└── loader.bin           # Loader 编译产物 (~1041字节)
```

## 🚀 启动流程

1. **BIOS 阶段**
   - BIOS 加载 MBR (第 0 扇区) 到内存 0x7C00
   - 验证魔数 0x55AA
   - 跳转到 0x7C00 执行

2. **MBR 阶段** (`3mbr.asm`)
   - 初始化段寄存器
   - 清屏
   - 显示 "hello OS"
   - 从硬盘读取 Loader (第 2 扇区开始，4 个扇区) 到 0x900
   - 跳转到 0x900 执行 Loader

3. **Loader 阶段** (`loader.asm`)
   - **检测物理内存**（E820 → E801 → 0x88）
   - 显示 "hello loader!"
   - **进入保护模式**：
     - 打开 A20 地址线
     - 加载 GDT
     - 设置 CR0.PE = 1
   - 初始化保护模式环境
   - 显示 'P'（表示成功进入保护模式）

## 🔧 编译和运行

### 编译

```bash
cd ~/code/汇编

# 编译 MBR
nasm -I include/ -o mbr.bin 3mbr.asm

# 编译 Loader
nasm -I include/ -o loader.bin loader.asm
```

### 写入镜像

```bash
# 写入 MBR（第 0 扇区）
dd if=mbr.bin of=~/bochs/disk/hd60M.img bs=512 count=1 conv=notrunc

# 写入 Loader（从第 2 扇区开始）
dd if=loader.bin of=~/bochs/disk/hd60M.img bs=512 count=4 seek=2 conv=notrunc
```

### 运行

```bash
cd ~/bochs
bochs -f bochsrc.disk
```

## 📊 内存布局

| 地址范围 | 用途 |
|----------|------|
| 0x0000~0x03FF | 中断向量表 (IVT) |
| 0x0400~0x04FF | BIOS 数据区 |
| 0x0500~0x7BFF | 可用内存 |
| 0x7C00~0x7DFF | MBR 加载区 (512 字节) |
| 0x7E00~0x8FFF | 可用内存 |
| 0x0900~0x0CFF | Loader 加载区 (~1024 字节) |
| 0x0B08 | `total_mem_bytes` (内存总量) |
| 0xB8000~0xBFFFF | 文本模式显存 |

## 🎨 显示颜色代码

### 属性字节格式

```
7  6  5  4  3  2  1  0
BL R  G  B  I  R  G  B
│  └──┴──┘  │  └──┴──┘
│   背景色  │   前景色
│           └─ 高亮位
└─ 闪烁位
```

### 常用颜色

| 值 | 颜色 |
|----|------|
| 0x07 | 黑底白字 |
| 0xB4 | 青底闪烁 + 红字 |
| 0xBF | 青底闪烁 + 亮白字 |
| 0x53 | 粉红底 + 青字 |

## 🔍 调试技巧

### Bochs 魔术断点

代码中使用 `xchg bx,bx` 作为魔术断点：

```asm
xchg bx,bx    ; Bochs 会自动在此处暂停
```

### 常用调试命令

```
bp 0x7c00          # 在 MBR 入口设置断点
bp 0x900           # 在 Loader 入口设置断点
c                  # 继续执行
s                  # 单步执行
n                  # 执行到下一指令（跳过call）
x /1wx 0xb08       # 查看内存(显示为十六进制)
r                  # 查看寄存器
u /20              # 反汇编 20 条指令
```

## 📝 关键数据结构

### ARDS (Address Range Descriptor Structure)

每个 ARDS 20 字节：

| 偏移 | 大小 | 名称 | 说明 |
|------|------|------|------|
| 0 | 8 | BaseAddrLow | 基地址低 32 位 |
| 8 | 8 | LengthLow | 长度低 32 位 |
| 16 | 4 | Type | 类型(1=可用,2=保留) |

### GDT 描述符 (8 字节)

```
63        56 55 54 53 52 51        48 47 46 45 44 43        40
┌──────────┬──┬──┬──┬──┬───────────┬──┬────┬──┬───────────┬──
│ Base     │G │D │L │AVL│ Limit    │P │DPL │S │   TYPE    │
│ 31:24    │  │  │  │   │ 19:16    │  │    │  │           │
└──────────┴──┴──┴──┴──┴───────────┴──┴────┴──┴───────────┴──

39                                16 15                      0
──────────────────────────────────┬──────────────────────────┐
  Base 23:16                      │  Limit 15:0              │
──────────────────────────────────┴──────────────────────────┘
```

## ⚠️ 常见问题

### 1. BIOS 不执行 MBR

**原因**：
- MBR 魔数不是 0x55AA
- 镜像路径错误
- Bochs 配置文件有误

**解决**：
```bash
# 验证魔数
xxd -s 510 -l 2 ~/bochs/disk/hd60M.img
# 应显示：000001fe: 55aa
```

### 2. 内存检测失败

**原因**：
- E820/E801/0x88 全部不支持（真机可能发生）
- 代码逻辑错误

**解决**：
- 使用更新的 BIOS/虚拟机
- 临时使用固定值（如 32MB）

### 3. 进入保护模式后挂起

**原因**：
- A20 未正确打开
- GDT 定义错误
- 段选择子错误

**解决**：
- 检查 GDT 结构
- 验证段选择子计算
- 使用 Bochs 调试查看寄存器状态

## 📚 参考资料

- Intel 80386 Programmer's Reference Manual
- BIOS Interrupt Call Reference
- Bochs User Manual

---

**最后更新**：2025-10-24
