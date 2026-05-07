#ifndef __WARMING_H__
#define __WARMING_H__

#include <common.h>

// 升温策略状态
#define WARMING_STATE_INACTIVE  0
#define WARMING_STATE_ACTIVE    1

// 温度阈值: 45度
#define CHIP_TEMP_THRESHOLD_WARMING  45

// 初始化升温策略
void warming_init(void);

// 启动升温策略
void start_warming_strategy(void);

// 停止升温策略
void stop_warming_strategy(void);

// 获取升温策略状态
int get_warming_state(void);

#endif