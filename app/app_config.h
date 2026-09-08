#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_MODE_BLINK    1
#define APP_MODE_KEY      2

/* 实验1使用 APP_MODE_BLINK；实验2和3使用 APP_MODE_KEY */
#define APP_MODE          APP_MODE_KEY

/* 分别改成 10U、50U、100U 重新编译 */
#define APP_BLINK_HZ      10U

#define APP_KEY_DEBOUNCE_MS 20U

#endif
