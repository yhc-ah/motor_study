#include "app_task.h"
#include "app_config.h"
#include "bsp_led.h"
#include "bsp_probe.h"
#include "bsp_key.h"
#include "stm32f4xx_hal.h"

#if (APP_BLINK_HZ != 10U) && \
    (APP_BLINK_HZ != 50U) && \
    (APP_BLINK_HZ != 100U)
#error "APP_BLINK_HZ must be 10, 50 or 100"
#endif

#define BLINK_HALF_PERIOD_MS (500U / APP_BLINK_HZ)

static uint32_t s_last_blink_tick;

static uint32_t s_seen_irq_count;
static uint32_t s_debounce_start;
static uint8_t  s_debounce_pending;

volatile uint32_t app_key_press_count;

void APP_Task_Init(void)
{
    BSP_LED_Init();
    BSP_Probe_Init();

    s_last_blink_tick = HAL_GetTick();

    s_seen_irq_count = key_irq_count;
    s_debounce_start = 0U;
    s_debounce_pending = 0U;
    app_key_press_count = 0U;
}

static void APP_BlinkTask(uint32_t now)
{
    if ((uint32_t)(now - s_last_blink_tick) >=
        BLINK_HALF_PERIOD_MS)
    {
        /*
         * 使用 += 而不是直接赋值 now，可减少长期累计漂移。
         * 前提是主循环始终足够快。
         */
        s_last_blink_tick += BLINK_HALF_PERIOD_MS;

        BSP_LED_Toggle();
        BSP_Probe_Toggle();
    }
}

static void APP_KeyTask(uint32_t now)
{
    uint32_t irq_snapshot = key_irq_count;

    /*
     * 收到新的中断后开始/重新开始消抖计时。
     * 若抖动产生多个上升沿，计时会从最后一次边沿重新开始。
     */
    if (irq_snapshot != s_seen_irq_count)
    {
        s_seen_irq_count = irq_snapshot;
        s_debounce_start = now;
        s_debounce_pending = 1U;
    }

    if ((s_debounce_pending != 0U) &&
        ((uint32_t)(now - s_debounce_start) >=
         APP_KEY_DEBOUNCE_MS))
    {
        s_debounce_pending = 0U;

        if (BSP_Key_IsPressed())
        {
            app_key_press_count++;
            BSP_LED_Toggle();
        }
    }
}

void APP_Task_Run(void)
{
    uint32_t now = HAL_GetTick();

#if APP_MODE == APP_MODE_BLINK
    APP_BlinkTask(now);
#elif APP_MODE == APP_MODE_KEY
    APP_KeyTask(now);
#else
#error "Invalid APP_MODE"
#endif
}