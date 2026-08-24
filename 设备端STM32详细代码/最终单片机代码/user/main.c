#include "stm32f10x.h"
#include "onenet.h"
#include "esp8266.h"
#include "usart.h"
#include "led.h"
#include "key.h"
#include "dht11.h"
#include "oled.h"
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define ESP8266_ONENET_INFO "AT+CIPSTART=\"TCP\",\"mqtts.heclouds.com\",1883\r\n"

/* ======================== 全局同步对象 ======================== */
static QueueHandle_t xSensorQueue = NULL;   // 传感器数据队列
static SemaphoreHandle_t xOledMutex = NULL; // OLED 显示互斥量
static SemaphoreHandle_t xEspMutex = NULL;  // ESP8266/MQTT 操作互斥量

/* 传感器数据结构体 */
typedef struct {
    u8 temp;
    u8 humi;
} SensorData_t;

/* 外部引用按键任务（在 key.c 中实现） */
extern void Task_KeyScan(void *pvParameters);


/* ======================== 纯软件延时（仅用于调度器启动前） ======================== */
// 不依赖 SysTick，不会干扰 FreeRTOS
static void SoftDelayMs(uint32_t ms)
{
    volatile uint32_t count = ms * 12000;
    while(count--);
}

/* ======================== 栈溢出钩子 ======================== */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    __asm volatile("BKPT #0");
    while(1);
}

/* ======================== 硬件初始化（仅启动时调用一次） ======================== */
static void Hardware_Init(void)
{
    // ⚠️ 已删除 Delay_Init()，FreeRTOS 会自动配置 SysTick
    
    Usart1_Init(115200);
    Usart2_Init(115200);
    Key_Init();
    Led_Init();
    OLED_Init();

    while(DHT11_Init()) {
        OLED_ShowString(0, 0, "DHT11 Error", 16);
        SoftDelayMs(1000);  // 使用纯软件延时
    }

    OLED_Clear();
    OLED_ShowString(0, 0, "Hardware init OK", 16);
    SoftDelayMs(1000);      // 使用纯软件延时
}

/* ======================== OLED 静态界面初始化 ======================== */
static void Display_Init(void)
{
    if(xSemaphoreTake(xOledMutex, portMAX_DELAY) == pdTRUE) {
        OLED_Clear();
        OLED_ShowCHinese(0, 0, 1);  // 温
        OLED_ShowCHinese(18, 0, 2); // 度
        OLED_ShowCHinese(36, 0, 0); // ：
        OLED_ShowCHinese(72, 0, 3); // ℃

        OLED_ShowCHinese(0, 3, 4);  // 湿
        OLED_ShowCHinese(18, 3, 5); // 度
        OLED_ShowCHinese(36, 3, 0); // ：
        OLED_ShowString(72, 3, "%", 16);

        OLED_ShowCHinese(0, 6, 6);  // 台
        OLED_ShowCHinese(18, 6, 7); // 灯
        OLED_ShowCHinese(36, 6, 0); // ：
        xSemaphoreGive(xOledMutex);
    }
}

/* ======================== 任务1：DHT11 数据采集 ======================== */
static void Task_DHT11(void *pvParameters)
{
    SensorData_t data;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for(;;) {
        vTaskSuspendAll();                              // 挂起调度器，保护 DHT11 时序
        DHT11_Read_Data(&data.temp, &data.humi);        
        xTaskResumeAll();                               // 恢复调度器

        xQueueSend(xSensorQueue, &data, pdMS_TO_TICKS(100));

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2000));
    }
}

/* ======================== 任务2：ESP8266 + MQTT 通信 ======================== */
static void Task_ESP8266(void *pvParameters)
{
    unsigned char *dataPtr = NULL;
    SensorData_t rxData;
    u8 retryCnt = 0;

    if(xSemaphoreTake(xOledMutex, portMAX_DELAY) == pdTRUE) {
        OLED_Clear();
        OLED_ShowString(0, 0, "Connect MQTT...", 16);
        xSemaphoreGive(xOledMutex); 
    }

    retryCnt = 0;
    while(ESP8266_SendCmd(ESP8266_ONENET_INFO, "CONNECT")) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retryCnt++;
        if(retryCnt > 20) { 
            UsartPrintf(USART_DEBUG, "MQTT connect timeout!\r\n");
            break; 
        }
    }

    if(retryCnt <= 20) {
        if(xSemaphoreTake(xOledMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            OLED_ShowString(0, 4, "MQTT Connected", 16);
            xSemaphoreGive(xOledMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));

        if(xSemaphoreTake(xOledMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            OLED_Clear();
            OLED_ShowString(0, 0, "Device login...", 16);
            xSemaphoreGive(xOledMutex);
        }

        retryCnt = 0;
        while(OneNet_DevLink()) {
            ESP8266_SendCmd(ESP8266_ONENET_INFO, "CONNECT");
            vTaskDelay(pdMS_TO_TICKS(500));
            retryCnt++;
            if(retryCnt > 20) {
                UsartPrintf(USART_DEBUG, "DevLink timeout!\r\n");
                break;
            }
        }
        OneNET_Subscribe();
    }

    Display_Init();

    for(;;) {
        if(xQueueReceive(xSensorQueue, &rxData, pdMS_TO_TICKS(100)) == pdTRUE) {
            if(xSemaphoreTake(xEspMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                OneNet_SendData(rxData.temp, rxData.humi);
                ESP8266_Clear();
                xSemaphoreGive(xEspMutex);
            }
        }
        
        if(xSemaphoreTake(xEspMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            dataPtr = ESP8266_GetIPD(0);
            if(dataPtr != NULL) {
                OneNet_RevPro(dataPtr);
            }
            xSemaphoreGive(xEspMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ======================== 任务3：OLED 刷新显示 ======================== */
static void Task_OLED(void *pvParameters)
{
    SensorData_t rxData;
    u8 buf[4];

    for(;;) {
        if(xQueueReceive(xSensorQueue, &rxData, portMAX_DELAY) == pdTRUE) {
            if(xSemaphoreTake(xOledMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                sprintf((char*)buf, "%2d", rxData.temp);
                OLED_ShowString(54, 0, buf, 16);

                sprintf((char*)buf, "%2d", rxData.humi);
                OLED_ShowString(54, 3, buf, 16);

                if(led_info.Led_Status) 
                    OLED_ShowCHinese(54, 6, 8); // 亮
                else 
                    OLED_ShowCHinese(54, 6, 9); // 灭

                xSemaphoreGive(xOledMutex);
            }
        }
    }
}

/* ======================== 主函数 ======================== */
int main(void)
{
    Hardware_Init();
    
    UsartPrintf(USART_DEBUG, "Creating objects...\r\n");
    xSensorQueue = xQueueCreate(5, sizeof(SensorData_t));
    xOledMutex = xSemaphoreCreateMutex();
    xEspMutex = xSemaphoreCreateMutex();
    configASSERT(xSensorQueue);
    configASSERT(xOledMutex);
    configASSERT(xEspMutex);
    
    UsartPrintf(USART_DEBUG, "Creating tasks...\r\n");
    xTaskCreate(Task_DHT11,   "DHT11",   256,  NULL, 1, NULL);
    xTaskCreate(Task_ESP8266, "ESP8266", 1536, NULL, 2, NULL);
    xTaskCreate(Task_OLED,    "OLED",    512,  NULL, 2, NULL);
    if(xTaskCreate(Task_KeyScan, "KeyScan", 128, NULL, 1, NULL) != pdPASS)
{
    // 如果创建失败，用已经验证能用的串口打印出来
    UsartPrintf(USART_DEBUG, ">>> Key Task Create FAILED! <<<\r\n");
}
else
{
    UsartPrintf(USART_DEBUG, ">>> Key Task Create SUCCESS <<<\r\n");
}
    
    UsartPrintf(USART_DEBUG, "Starting scheduler...\r\n"); 
    vTaskStartScheduler();
    
    UsartPrintf(USART_DEBUG, "ERROR: Scheduler returned!\r\n"); 
    while(1);
}