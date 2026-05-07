#include <warming.h>
#include <dbgi2c.h>
#include <multiphase.h>
#include <debug.h>
#include <system.h>
#include <chip.h>
#include <freq.h>

// 升温策略状态变量
static int warming_state = WARMING_STATE_INACTIVE;

// TPU频率设置地址和值
#define TPU_FREQ_ADDR   (0x7050000000ULL + 0x00D4ULL)
#define TPU_FREQ_VALUE  0x51041060
#define TPU_FREQ_TEST   0x51041050

#define CC_SYS_RESET    0x7050003008ULL

// TPU寄存器地址定义
#define TPU_REG_BASE_0  0x6908050000ULL
#define TPU_REG_BASE_1  0x6918050000ULL
#define TPU_REG_BASE_2  0x6928050000ULL
#define TPU_REG_BASE_3  0x6938050000ULL

// Clock/Reset寄存器偏移
#define REG_CLOCK_SET   0x0000
#define REG_CLOCK_CLR   0x0004
#define REG_TPU_ENABLE  0x0100

// 注意：两颗芯片的基址相同，通过dbgi2c_write32的chip参数区分
// TPU寄存器地址定义
#define TPU_MUL_BASE_0  0x6908000000ULL
#define TPU_MUL_BASE_1  0x6918000000ULL
#define TPU_MUL_BASE_2  0x6928000000ULL
#define TPU_MUL_BASE_3  0x6938000000ULL

// TPU MUL指令数组 (16条指令)
static const uint32_t tpu_mul_values[16] = {
    0x00000001,  // offset 0x00
    0x78400620,  // offset 0x04
    0x0036c092,  // offset 0x08
    0x10001000,  // offset 0x0C
    0x54000380,  // offset 0x10
    0x0002ab00,  // offset 0x14
    0x00000000,  // offset 0x18
    0x00015580,  // offset 0x1C
    0x00000000,  // offset 0x20
    0x00000000,  // offset 0x24
    0x00000000,  // offset 0x28
    0x00000000,  // offset 0x2C
    0x00100000,  // offset 0x30
    0x00000000,  // offset 0x34
    0x00000001,  // offset 0x38
    0x00000100,  // offset 0x3C
};

void warming_init(void)
{
    warming_state = WARMING_STATE_INACTIVE;
    dbg_printf("[Warming] initialized, state = %d\n", warming_state);
}

void start_warming_strategy(void)
{
#define TPU_MPLL		2
#define PARENT_FREQ		25 * MHZ
    int i;
    int a;
    int chip;
    int ret = 0;
    uint32_t value;

    dbg_printf("[Warming] starting warming strategy...\n");

    // 1. 设置VDDR电压为0.85V (850mV)
    dbg_printf("[Warming] Step 1: Set VDDR voltage to 0.85V\n");
    for (chip = 0; chip < SOC_NUM; chip++) {
        ret = multiphase_set_out_voltage(0, chip, 850);
        dbg_printf("[Warming]   chip-%d voltage set %s\n", chip, ret ? "FAILED" : "OK");
    }


    // 2. 设置TPU频率为1.2GHz
    dbg_printf("[Warming] Step 2: Set TPU freq to 1.2GHz\n");
    for (chip = 0; chip < SOC_NUM; chip++) {
        ret = dbgi2c_read32(chip, TPU_FREQ_ADDR, &value);
        dbg_printf("[Warming] before  chip-%d addr: %lx%08lx, value = %x\n", chip, (uint32_t)(TPU_FREQ_ADDR >> 32), (uint32_t)(TPU_FREQ_ADDR & 0xFFFFFFFF), value);
        ret = dbgi2c_write32(chip, TPU_FREQ_ADDR, TPU_FREQ_TEST);
        // ret = sg2044_clk_pll_set_rate(chip, TPU_MPLL, 1200 * MHZ, PARENT_FREQ);
        dbg_printf("[Warming]   chip-%d addr: %lx%08lx, freq set %s\n", chip, (uint32_t)(TPU_FREQ_ADDR >> 32), (uint32_t)(TPU_FREQ_ADDR & 0xFFFFFFFF), ret ? "FAILED" : "OK");
    }

    // 3. cc_sys 复位
    dbg_printf("[Warming] Step 2: cc_sys reset\n");
    for (chip = 0; chip < SOC_NUM; chip++) {
        ret = dbgi2c_write32(chip, CC_SYS_RESET, 0xFFFFFFFF);
        dbg_printf("[Warming]   chip-%d, addr: %lx%08lx, CC SYS RESET %s\n", chip, (uint32_t)((CC_SYS_RESET) >> 32), (uint32_t)((CC_SYS_RESET) & 0xFFFFFFFF), ret ? "FAILED" : "OK");
    }
    
    // 4. 释放TPU/GDMA/SDMA的clock和reset
    dbg_printf("[Warming] Step 4: Release clock and reset\n");
    uint64_t clock_bases[] = {TPU_REG_BASE_0, TPU_REG_BASE_1, TPU_REG_BASE_2, TPU_REG_BASE_3};
    for (chip = 0; chip < SOC_NUM; chip++) {
        for (a = 0; a < 4; a++) {
            ret = dbgi2c_write32(chip, clock_bases[a] + REG_CLOCK_SET, 0xFFFFFFFF);
            dbg_printf("[Warming]   chip-%d core-%d, addr: %lx%08lx, CLOCK_SET %s\n", chip, a, (uint32_t)((clock_bases[a] + REG_CLOCK_SET) >> 32), (uint32_t)((clock_bases[a] + REG_CLOCK_SET) & 0xFFFFFFFF), ret ? "FAILED" : "OK");
        }
    }
    for (chip = 0; chip < SOC_NUM; chip++) {
        for (a = 0; a < 4; a++) {
            ret = dbgi2c_write32(chip, clock_bases[a] + REG_CLOCK_CLR, 0x00000000);
            dbg_printf("[Warming]   chip-%d core-%d, addr: %lx%08lx, CLOCK_CLR %s\n", chip, a, (uint32_t)((clock_bases[a] + REG_CLOCK_CLR) >> 32), (uint32_t)((clock_bases[a] + REG_CLOCK_CLR) & 0xFFFFFFFF), ret ? "FAILED" : "OK");
        }
    }

    // 5. 使能TPU
    uint64_t tpu_mul_base[] = {TPU_MUL_BASE_0, TPU_MUL_BASE_1, TPU_MUL_BASE_2, TPU_MUL_BASE_3};
    dbg_printf("[Warming] Step 5: Enable TPU\n");
    for (chip = 0; chip < SOC_NUM; chip++) {
        for (a = 0; a < 4; a++) {
            ret = dbgi2c_write32(chip, tpu_mul_base[a] + REG_TPU_ENABLE, 0x4C000001);
            dbg_printf("[Warming]   chip-%d core-%d, addr: %lx%08lx, TPU enable %s\n", chip, a, (uint32_t)((tpu_mul_base[a] + REG_TPU_ENABLE) >> 32), (uint32_t)((tpu_mul_base[a] + REG_TPU_ENABLE) & 0xFFFFFFFF), ret ? "FAILED" : "OK");
        }
    }

    // 6. 执行TPU MUL指令 (2 chips x 4 cores x 16 instructions = 128 writes)
    dbg_printf("[Warming] Step 6: Execute TPU MUL commands\n");
    dbg_printf("[Warming]   Total: 2 chips x 4 cores x 16 instructions = 128 writes\n");
    for (chip = 0; chip < SOC_NUM; chip++) {
        for (a = 0; a < 4; a++) {
            uint64_t base = tpu_mul_base[a];
            // dbg_printf("[Warming] chip-%d tpu-%d base=0x%lx%08lx\n", chip, a, base);
            dbg_printf("[Warming] chip-%d tpu-%d base=0x%lx%08lx\n", chip, a,  (uint32_t)(base >> 32), (uint32_t)(base & 0xFFFFFFFF));
            
            for (i = 0; i < 16; i++) {
                uint64_t addr = base + (i * 4);
                ret = dbgi2c_write32(chip, addr, tpu_mul_values[i]);
                dbg_printf("[%d] addr=0x%lx%08lx, value=0x%x %s\n", 
                           i, (uint32_t)(addr >> 32), (uint32_t)(addr & 0xFFFFFFFF), tpu_mul_values[i], ret ? "FAILED" : "OK");
            }
        }
    }

    warming_state = WARMING_STATE_ACTIVE;
    dbg_printf("[Warming] warming strategy started successfully, state=%d\n", warming_state);
}

void stop_warming_strategy(void)
{
    int i;
    int ret;

    dbg_printf("[Warming] stopping warming strategy...\n");

    // 1. 恢复VDDR电压为0.8V (800mV)
    dbg_printf("[Warming] Step 1: Restore VDDR voltage to 0.8V\n");
    for (i = 0; i < SOC_NUM; i++) {
        ret = multiphase_set_out_voltage(0, i, 800);
        dbg_printf("[Warming]   chip-%d voltage restore %s\n", i, ret ? "FAILED" : "OK");
    }

    // 2. 复位芯片 (sys_rst_disable)
    dbg_printf("[Warming] Step 2: Reset chips via sys_rst\n");
    sys_rst_disable();
    dbg_printf("[Warming]   sys_rst_disable called\n");

    warming_state = WARMING_STATE_INACTIVE;
    dbg_printf("[Warming] warming strategy stopped, state=%d\n", warming_state);
}

int get_warming_state(void)
{
    return warming_state;
}