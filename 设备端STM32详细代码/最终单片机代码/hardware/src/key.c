#include "stm32f10x.h"
#include "key.h"
#include "led.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "usart.h"


/*
************************************************************
*	函数名称：	Key_Init
*	说明：		仅初始化 GPIO，不再配置 EXTI 中断
************************************************************
*/
void Key_Init(void)
{
    GPIO_InitTypeDef gpio_initstruct;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
     RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE); // ?? 必须加上这一行！
    // PA1 配置为上拉输入
    gpio_initstruct.GPIO_Mode = GPIO_Mode_IPU;
    gpio_initstruct.GPIO_Pin = GPIO_Pin_1;
    gpio_initstruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio_initstruct);
    
    // ?? 删除了 EXTI 和 NVIC 配置，改由 RTOS 任务轮询
}

/*
************************************************************
*	函数名称：	Task_KeyScan
*	说明：		按键扫描任务（在 main.c 中创建）
*           利用 vTaskDelay 实现消抖，不占用中断资源
************************************************************
*/
void Task_KeyScan(void *pvParameters)
{
    uint8_t key_last = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1); // ? 用上电实际状态初始化，避免误触发
    uint8_t key_current;
    
    for(;;)
    {       // 每 1 秒打印一次，证明任务活着
    static uint32_t tick_cnt = 0;
    if(++tick_cnt % 100 == 0) 
        UsartPrintf(USART_DEBUG, "Key Task Alive\r\n"); 

    key_current = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
        key_current = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
        
        // 检测到按下（低电平），且上一次是松开状态
        if(key_current == 0 && key_last == 1)
        {
            vTaskDelay(pdMS_TO_TICKS(20)); // ? 20ms 消抖
            
            // 再次确认是否真的按下
            if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == 0)
            {
                UsartPrintf(USART_DEBUG, ">>> KEY PRESSED <<<\r\n");
                
                if(led_info.Led_Status == LED_ON) 
                    Led_Set(LED_OFF);
                else 
                    Led_Set(LED_ON);
                
                // 等待按键释放
                while(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1) == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(10)); // ? 10ms 释放检测周期
                }
            }
        }
        
        key_last = key_current;
        vTaskDelay(pdMS_TO_TICKS(10)); // ? 10ms 扫描周期
    }
}