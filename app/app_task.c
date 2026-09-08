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

#if APP_MODE == APP_MODE_BLINK
static uint32_t s_last_blink_tick;
#elif APP_MODE == APP_MODE_KEY
static uint32_t s_seen_irq_count;
static uint32_t s_debounce_start;
static uint8_t  s_debounce_pending;
#endif

volatile uint32_t app_key_press_count;

void APP_Task_Init(void)
{
    BSP_LED_Init();
    BSP_Probe_Init();

#if APP_MODE == APP_MODE_BLINK
    s_last_blink_tick = HAL_GetTick();
#elif APP_MODE == APP_MODE_KEY
    s_seen_irq_count = key_irq_count;
    s_debounce_start = 0U;
    s_debounce_pending = 0U;
#endif
    app_key_press_count = 0U;
}

#if APP_MODE == APP_MODE_BLINK
static void APP_BlinkTask(uint32_t now)
{
    if ((uint32_t)(now - s_last_blink_tick) >=
        BLINK_HALF_PERIOD_MS)
    {
        /*
         * 浣跨敤 += 鑰屼笉鏄洿鎺ヨ祴鍊?now锛屽彲鍑忓皯闀挎湡绱婕傜Щ銆?
         * 鍓嶆彁鏄富寰幆濮嬬粓瓒冲蹇€?
         */
        s_last_blink_tick += BLINK_HALF_PERIOD_MS;

        BSP_LED_Toggle();
        BSP_Probe_Toggle();
    }
}
#endif

#if APP_MODE == APP_MODE_KEY
static void APP_KeyTask(uint32_t now)
{
    uint32_t irq_snapshot = key_irq_count;

    /*
     * 鏀跺埌鏂扮殑涓柇鍚庡紑濮?閲嶆柊寮€濮嬫秷鎶栬鏃躲€?
     * 鑻ユ姈鍔ㄤ骇鐢熷涓笂鍗囨部锛岃鏃朵細浠庢渶鍚庝竴娆¤竟娌块噸鏂板紑濮嬨€?
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

#endif
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
