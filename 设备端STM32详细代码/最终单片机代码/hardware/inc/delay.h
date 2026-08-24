#ifndef _DELAY_H_
#define _DELAY_H_

#include "FreeRTOS.h"
#include "task.h"

/*
 * FreeRTOS 兼容层
 * 原 delay.c 中的阻塞式 SysTick 延时已废弃，
 * 此处将原有接口映射到 FreeRTOS 任务安全延时 API。
 */

/* Delay_Init 保留声明，delay.c 中需提供空实现 */
void Delay_Init(void);

/* 毫秒级延时 → vTaskDelay（宏替换，无 inline 兼容性问题） */
#define DelayXms(ms)    vTaskDelay(pdMS_TO_TICKS((unsigned short)(ms)))
#define DelayMs(ms)     vTaskDelay(pdMS_TO_TICKS((unsigned short)(ms)))

/*
 * 微秒级延时 → 忙等近似
 * 使用 __inline 替代 static inline，兼容 ARMCC V5 C90 模式
 */
static __inline void DelayUs(unsigned short us)
{
    volatile unsigned int i = (unsigned int)us * 9U;
    while (i--) {}
}

#endif /* _DELAY_H_ */