# BMC 通过 I2C2 对 MCU 进行固件升级 — 接口文档

## 概述

MCU 提供一组 I2C 从机接口供 BMC 使用，支持**遥测数据读取**和 **Flash 编程（用于固件升级）**。

| I2C 总线 | 连接目标 | 从机地址 | 功能 |
|---------|---------|---------|------|
| **I2C2** | **BMC** | **`0x60 + module_id × 2`** | **遥测只读 + Flash 编程** |

本文档面向 BMC 固件开发人员，说明如何通过 **I2C2** 接口对 MCU 进行 Flash 编程和固件升级。

---

## 1. I2C 从机地址

### 1.1 地址计算

BMC 从机地址由槽位 ID（module_id）决定：

```
I2C2 从机地址 = 0x60 + module_id × 2
```

槽位 ID 由硬件引脚状态决定：

| module_id | 从机地址 |
|-----------|---------|
| 0 | 0x60 |
| 1 | 0x62 |
| 2 | 0x64 |
| 3 | 0x66 |
| ... | 以此类推 |

地址 mask = 0x01，即每个槽位匹配两个连续地址（如 module 0 匹配 0x60 和 0x61）。

### 1.2 I2C 通信参数

| 参数 | 值 |
|------|-----|
| 速率 | 100 kHz（标准模式） |
| 地址长度 | 7-bit |
| 写操作格式 | `[START] [地址+W] [索引字节] [数据字节0] [数据字节1] ... [STOP]` |
| 读操作格式 | `[START] [地址+W] [索引字节] [STOP] [START] [地址+R] [数据字节0] ... [STOP]` |

> **重要**：每次 I2C 传输必须以 **STOP 条件**结束。STOP 条件是触发 Flash 写入和命令提交的关键事件。

---

## 2. 寄存器映射

BMC 接口采用 **寄存器组模型**（indexed register file），通过索引字节选择寄存器，支持地址自动递增。

### 2.1 寄存器访问方式

```
写寄存器：
  [S] [0x60+W] [idx] [data0] [data1] ... [P]
  └─ 首字节设置寄存器索引 idx
      └─ 后续字节写入 idx, idx+1, idx+2...

读寄存器：
  [S] [0x60+W] [idx] [P]   ← 设置索引
  [S] [0x60+R] [data0] ... [P]  ← 连续读取
```

### 2.2 完整寄存器表

#### 只读遥测寄存器（索引 0x00 ~ 0x18）

| 索引 | 名称 | 说明 | 访问 |
|------|------|------|------|
| 0x00 | SOC_TEMP | SoC 温度（°C） | RO |
| 0x01 | BOARD_TEMP | 板卡温度（°C） | RO |
| 0x02 | **BOARD_TYPE** | **板卡类型（HD12=0xb6, RH12=0xb7）** | **RO** |
| 0x03 | VERSION_L | MCU 固件版本号 | RO |
| 0x04 | VERSION_H | 硬件版本号 | RO |
| 0x05 | 12V_POWER_L | 12V 功率低字节 | RO |
| 0x06 | 12V_POWER_H | 12V 功率高字节 | RO |
| 0x07~0x17 | SN[0..16] | 序列号（17 字节，ASCII） | RO |
| 0x18 | SOC_INDEX | SoC 索引 | RO |
| **0x19** | **MCU_FAMILY** | **MCU 家族（GD32F4=2）** | **RO** |

#### 命令寄存器（索引 0x70）

| 索引 | 名称 | 说明 | 访问 |
|------|------|------|------|
| **0x70** | **CMD** | 命令寄存器 | **WO** |

支持的命令值：

| 命令 | 值 | 说明 |
|------|----|------|
| CMD_UPDATE | **0x08** | 启动 I2C 升级模式 |

> 命令在 I2C **STOP 条件**时提交，MCU 在内部循环中异步执行。

#### Flash 编程寄存器（索引 0x63~0x65, 0x7c~0x7f, 0x80~0xff）

| 索引 | 名称 | 说明 | 访问 |
|------|------|------|------|
| **0x63** | **FLASH_CMD** | Flash 命令 | **WO** |
| **0x64** | **EXPECT_TYPE** | **解锁前写入期望板型** | **WO** |
| **0x65** | **EXPECT_FAMILY** | **解锁前写入期望 MCU 家族** | **WO** |
| **0x7c** | FLASH_OFFSET[0] | Flash 偏移地址——字节 0（最高字节，big-endian） | **RW** |
| **0x7d** | FLASH_OFFSET[1] | Flash 偏移地址——字节 1 | **RW** |
| **0x7e** | FLASH_OFFSET[2] | Flash 偏移地址——字节 2 | **RW** |
| **0x7f** | FLASH_OFFSET[3] | Flash 偏移地址——字节 3（最低字节） | **RW** |
| **0x80~0xfe** | FLASH_DATA[0..126] | Flash 写数据缓冲区（127 字节） | **RW** |
| **0xff** | **FLASH_FLUSH** | 写入第 128 个字节，**STOP 时触发 Flash 写入** | **WO** |

Flash 命令（写入 0x63 寄存器）：

| 命令 | 值 | 说明 |
|------|----|------|
| UNLOCK | **0x02** | 解锁 Flash 控制器，允许擦除和编程 |
| LOCK | **0x03** | 锁定 Flash 控制器 |
| ERASE | **0x04** | 擦除当前偏移地址所在的 4KB 页 |

Flash 偏移地址（0x7c~0x7f）采用 **大端序**：

```
offset = flash_offset[0] << 24 |
         flash_offset[1] << 16 |
         flash_offset[2] <<  8 |
         flash_offset[3]
```

> **注意**：未列出的索引读取时返回 0xff，写入时被忽略。

---

## 3. Flash 编程操作

### 3.1 基本流程

Flash 单次写入操作遵循以下顺序：

```
1. 解锁 MCU         → 写 0xb6 到 EXPECT_TYPE(0x64)、写 0x02 到 EXPECT_FAMILY(0x65)
2. 解锁 Flash       → 写 0x02 到 FLASH_CMD(0x63)
3. 设置偏移地址      → 写 4 字节到 FLASH_OFFSET(0x7c~0x7f)
4. 擦除页           → 写 0x04 到 FLASH_CMD(0x63)
5. 写入数据（128 字节）
                   → 写数据到 FLASH_DATA(0x80~0xff)
6. 锁定 Flash       → 写 0x03 到 FLASH_CMD(0x63)
```

> **重要**：EXPECT_TYPE 和 EXPECT_FAMILY 必须先于所有 Flash 操作写入，否则擦除/写入命令会被 MCU 静默拒绝。写入顺序必须先 EXPECT_TYPE 再 EXPECT_FAMILY——写后者时 MCU 立即校验，两个值都匹配（EXPECT_TYPE == 板型号 && EXPECT_FAMILY == MCU 家族）才解锁 Flash 访问。

> **偏移地址限制**：
> - 擦除操作要求偏移地址与 4KB 页对齐（`offset & 0xFFF == 0`），不对齐时擦除命令被忽略
> - Flash 写入范围必须在 `[0, 1MB)` 内，超出则命令被忽略
> - 数据写入地址 = `FLASH_BASE + (flash_offset & ~0x7F)`，自动 128 字节对齐

### 3.2 辅助函数定义

以下 Python 伪代码定义后续全量升级流程中会用到的辅助函数：

```python
# I2C 通信封装（由 BMC 平台层实现）
def i2c_write(addr, data):    # 写数据到 I2C 从机
def i2c_read(addr, reg, len): # 从 I2C 从机读取 len 字节

def flash_unlock():
    """解锁 MCU Flash 访问，必须先写 EXPECT_TYPE + EXPECT_FAMILY"""
    i2c_write(0x60, [0x64, 0xb6])   # EXPECT_TYPE = HD12
    i2c_write(0x60, [0x65, 0x02])   # EXPECT_FAMILY = GD32F4
    i2c_write(0x60, [0x63, 0x02])   # FLASH_CMD = UNLOCK

def flash_lock():
    i2c_write(0x60, [0x63, 0x03])   # 写 FLASH_CMD = LOCK

def flash_set_offset(offset):
    """设置 Flash 偏移地址（4 字节大端序）"""
    i2c_write(0x60, [0x7c,
                     (offset >> 24) & 0xFF,
                     (offset >> 16) & 0xFF,
                     (offset >> 8)  & 0xFF,
                     offset & 0xFF])

def flash_erase_page(page_aligned_offset):
    """擦除一个 4KB 页，offset 必须 4KB 对齐"""
    flash_unlock()
    flash_set_offset(page_aligned_offset)
    i2c_write(0x60, [0x63, 0x04])   # FLASH_CMD = ERASE
    flash_lock()

def flash_write_128(offset, data):
    """写入 128 字节数据，STOP 时 MCU 自动写入 Flash"""
    assert len(data) == 128
    flash_set_offset(offset)
    buf = [0x80] + data        # 从 FLASH_DATA (0x80) 开始写
    i2c_write(0x60, buf)       # 最后一个字节写入 0xff → 标记 flush

def flash_read(offset, length):
    """从 Flash 读取数据"""
    result = []
    while length > 0:
        slen = min(length, 128)
        flash_set_offset(offset)
        chunk = i2c_read(0x60, 0x80, slen)  # 从 FLASH_DATA 读
        result += chunk
        offset += slen
        length -= slen
    return result
```

### 3.3 部分填充（少于 128 字节）

Flash 写入缓冲区中的 **0xFFFFFFFF 会被跳过**，因此可以在构造数据时填充 0xFF：

```python
# 写入 64 字节（填充至 128 字节，0xFF 字会被跳过）
data = actual_data + [0xFF] * (128 - len(actual_data))
flash_write_128(offset, data)
```

---

## 4. 固件升级命令 (CMD_UPDATE)

写入命令 **0x08**（CMD_UPDATE）到寄存器 0x70：

```
[S] [0x60+W] [0x70] [0x08] [P]
│            └─ CMD 寄存器  └─ CMD_UPDATE
└─ STOP → MCU 异步执行升级引导
```

CMD_UPDATE 执行后，MCU 将：
1. 停止正常运行，禁用除当前 I2C 从机外的其他内部功能
2. 等待引导加载程序通过 I2C 接收新的固件数据
3. 验证后跳转至新固件

---

## 5. BMC 全量升级流程（推荐流程）

BMC 进行 MCU 固件升级时应遵循以下完整流程。全量升级擦写整个固件区域（从偏移 0 开始），不区分 bootloader/app 区。

### 5.1 固件文件格式

MCU 固件文件末尾附加 128 字节的 `fwinfo` 结构体：

```
  [0]                      [file_size - 128]  [file_size - 1]
  +-------------------------+------------------+
  |  固件二进制数据         |  fwinfo (128B)   |
  |  (file_size - 128 字节) |                  |
  +-------------------------+------------------+
                              | md5[0..15]  | ← 固件数据的 MD5 校验值
                              | magic[0..3] | ← "MCUF"
                              | type        | ← 目标板型（如 HD12=0xb6）
                              | mcu_family  | ← MCU 系列（GD32F4xx=2）
                              | r0[2]       | ← 保留
                              | timestamp   | ← 时间戳
                              | r1[100]     | ← 保留
```

### 5.2 完整升级流程

```
┌─────────────────────────────────────────────┐
│  Step 1: 固件校验（BMC 侧本地执行）           │
│                                             │
│  1. 打开固件文件，检查尾部 magic == "MCUF"    │
│  2. 计算固件数据 (file_size-128) 的 MD5       │
│     与 fwinfo.md5 对比                       │
│  3. 读取 MCU 的 BOARD_TYPE (索引 0x02)         │
│     与 fwinfo.type 对比确认兼容性             │
│  4. 读取 MCU 的 MCU_FAMILY (索引 0x19)         │
│     与 fwinfo.mcu_family 对比                │
│  5. 记录当前 VERSION_L (索引 0x03) 作为备份   │
│                                             │
│  任何一步失败 → 终止升级                      │
├─────────────────────────────────────────────┤
│  Step 2: 解锁 MCU Flash 操作                 │
│                                             │
│  i2c_write(0x60, [0x64, fwinfo.type])       │
│  i2c_write(0x60, [0x65, fwinfo.mcu_family]) │
│                                             │
│  写入 EXPECT_FAMILY 时 MCU 立即校验，        │
│  匹配才允许后续 Flash 擦写                   │
├─────────────────────────────────────────────┤
│  Step 3: 全片擦除                            │
│                                             │
│  for offset in range(0, firmware_size, 4KB): │
│      flash_erase_page(offset)                │
├─────────────────────────────────────────────┤
│  Step 4: 写入固件数据                        │
│                                             │
│  for offset in range(0, firmware_size, 128): │
│      block = firmware[offset:offset+128]     │
│      block += [0xFF] * (128 - len(block))    │
│      flash_write_128(offset, block)          │
├─────────────────────────────────────────────┤
│  Step 5: 写后验证（读回比较）                 │
│                                             │
│  readback = flash_read(0, firmware_size)     │
│  if readback != firmware:                    │
│      终止升级，记录错误                       │
├─────────────────────────────────────────────┤
│  Step 6: 触发升级引导                        │
│                                             │
│  i2c_write(0x60, [0x70, 0x08])  # CMD_UPDATE│
└─────────────────────────────────────────────┘
```

### 5.3 完整升级流程代码示例

```python
def mcu_full_upgrade(firmware_file):
    """MCU 全量固件升级"""
    
    # ---- Step 1: 固件校验 ----
    with open(firmware_file, 'rb') as f:
        firmware = list(f.read())
    
    file_size = len(firmware)
    if file_size < 128:
        print("ERROR: firmware file too small")
        return False
    
    fwinfo = firmware[-128:]  # 尾部 128 字节
    
    # 检查 magic
    if fwinfo[16:20] != [0x4D, 0x43, 0x55, 0x46]:  # "MCUF"
        print("ERROR: invalid magic (expected 'MCUF')")
        return False
    
    # 检查 MD5
    fw_data = firmware[:-128]
    expected_md5 = bytes(fwinfo[0:16])
    calculated_md5 = calc_md5(bytes(fw_data))
    if calculated_md5 != expected_md5:
        print("ERROR: MD5 mismatch")
        return False
    
    # 读取 MCU 板型（索引 0x02）
    board_type = i2c_read(0x60, 0x02, 1)[0]
    fw_board_type = fwinfo[20]  # fwinfo.type
    if board_type != fw_board_type:
        print(f"ERROR: board type mismatch (MCU=0x{board_type:02x}, FW=0x{fw_board_type:02x})")
        return False
    
    # 读取 MCU 家族（索引 0x19）
    mcu_family = i2c_read(0x60, 0x19, 1)[0]
    fw_family = fwinfo[21]  # fwinfo.mcu_family
    if mcu_family != fw_family:
        print(f"ERROR: MCU family mismatch (MCU={mcu_family}, FW={fw_family})")
        return False
    
    print("Firmware validation passed")
    
    # ---- Step 2: 解锁 MCU Flash ----
    print("Unlocking MCU flash...")
    i2c_write(0x60, [0x64, fw_board_type])   # EXPECT_TYPE
    i2c_write(0x60, [0x65, fw_family])        # EXPECT_FAMILY
    
    # ---- Step 3: 擦除 ----
    PAGE_SIZE = 4 * 1024
    FLASH_BUF = 128
    
    print("Erasing flash...")
    for offset in range(0, file_size, PAGE_SIZE):
        flash_erase_page(offset)
    print("Erase done")
    
    # ---- Step 3: 写入 ----
    print("Programming flash...")
    for offset in range(0, file_size, FLASH_BUF):
        block = list(fw_data[offset:offset + FLASH_BUF])
        block += [0xFF] * (FLASH_BUF - len(block))
        flash_write_128(offset, block)
    print("Program done")
    
    # ---- Step 4: 写后验证 ----
    print("Verifying flash...")
    readback = flash_read(0, file_size)
    if readback != firmware:
        print("ERROR: verify failed")
        return False
    print("Verify passed")
    
    # ---- Step 5: 触发升级 ----
    print("Triggering upgrade...")
    i2c_write(0x60, [0x70, 0x08])  # CMD_UPDATE
    
    print("Upgrade completed successfully")
    return True
```

### 5.4 超时控制建议

| 阶段 | 参考时间 | 建议等待 |
|------|---------|---------|
| 每页擦除（4KB） | < 100ms | 200ms |
| 每 128 字节写入 | ~ 6.4ms | 50ms |
| 每次 I2C 传输间隔 | — | ≥ 10ms |
| 全量升级 80KB 固件 | ~ 5s | — |

---

## 6. 限制与注意事项

### 6.1 Flash 操作约束

| 约束 | 说明 |
|------|------|
| **擦除粒度** | 最小 4KB 页擦除 |
| **写入粒度** | 每次最多 128 字节，按字（32-bit）写入 |
| **对齐要求** | 擦除偏移必须 4KB 对齐；数据写入 128 字节自动对齐 |
| **寿命** | Flash 典型擦写寿命 10 万次 |
| **写入前必须擦除** | Flash 只能将 1 写为 0，不能将 0 写为 1。写入非 0xFF 的地址需先擦除 |

### 6.2 错误处理

| 异常情况 | 说明 |
|---------|------|
| 擦除失败 | 通常因偏移未 4KB 对齐或超出 Flash 范围；也可能是未写 EXPECT_TYPE/EXPECT_FAMILY，MCU 静默拒绝 |
| 写入失败 | 常见原因：Flash 未先擦除，Flash 已锁定，或未解锁 MCU Flash（未写 EXPECT 寄存器） |
| MD5 校验不通过 | 固件文件损坏或不是正确的 MCU 固件 |
| 板型不匹配 | 固件 `fwinfo.type` 与 MCU `BOARD_TYPE` 不一致，EXPECT_TYPE 校验失败 |
| MCU 家族不匹配 | 固件 `fwinfo.mcu_family` 与 MCU `MCU_FAMILY` 不一致，EXPECT_FAMILY 校验失败 |
| 写后验证失败 | Flash 编程异常，建议重新擦除并重试 |
| 读写无响应 | I2C 从机正在执行 Flash 操作时可能出现短暂无响应，建议 BMC 实现重试机制 |

---

## 7. 附录：寄存器对照表（完整版）

| 索引 | 寄存器 | 读 | 写 | 说明 |
|------|--------|----|----|------|
| 0x00 | SOC_TEMP | ✅ SoC 温度 | — | |
| 0x01 | BOARD_TEMP | ✅ 板温 | — | |
| 0x02 | **BOARD_TYPE** | ✅ **板型号** | — | |
| 0x03 | VERSION_L | ✅ 固件版本 | — | |
| 0x04 | VERSION_H | ✅ 硬件版本 | — | |
| 0x05 | 12V_POWER_L | ✅ 功率低字节 | — | |
| 0x06 | 12V_POWER_H | ✅ 功率高字节 | — | |
| 0x07~0x17 | SN | ✅ 序列号 | — | 17 字节 |
| 0x18 | SOC_INDEX | ✅ SoC 索引 | — | |
| **0x19** | **MCU_FAMILY** | **✅ MCU 家族** | **—** | **GD32F4=2** |
| **0x63** | **FLASH_CMD** | **—** | **✅ 命令** | **解锁/锁定/擦除** |
| **0x64** | **EXPECT_TYPE** | **—** | **✅ 板型** | **与 0x02 比对** |
| **0x65** | **EXPECT_FAMILY** | **—** | **✅ 家族** | **与 0x19 比对，写时校验** |
| **0x70** | **CMD** | **—** | **✅ 命令** | **CMD_UPDATE** |
| **0x7c** | **FLASH_OFFSET[0]** | **✅** | **✅** | **偏移[31:24]** |
| **0x7d** | **FLASH_OFFSET[1]** | **✅** | **✅** | **偏移[23:16]** |
| **0x7e** | **FLASH_OFFSET[2]** | **✅** | **✅** | **偏移[15:8]** |
| **0x7f** | **FLASH_OFFSET[3]** | **✅** | **✅** | **偏移[7:0]** |
| **0x80~0xfe** | **FLASH_DATA** | **✅ 读 Flash** | **✅ 写入** | **数据缓冲区 127B** |
| **0xff** | **FLASH_FLUSH** | **✅** | **✅ 触发写入** | **第 128B + STOP 触发** |
| 其他 | — | 0xff | 忽略 | |

---