#ifndef WEEK3_CONFIG_H
#define WEEK3_CONFIG_H
/* Review docs/week-03-setup.md before connecting modules or enabling options. */
#define WEEK3_UART_BAUD 460800U
#define WEEK3_I2C_HZ 400000U
/* Default status polling works without an invented INT route. For EXTI,
 * wire MPU INT to PC4 after checking board conflicts, then enable this. */
#define WEEK3_MPU_PC4_INT 0
/* No region is assumed free. Fill BOTH only after reviewing Flash allocation. */
#define WEEK3_FLASH_TEST_BASE 0U
#ifndef WEEK3_FLASH_TEST_SIZE
#define WEEK3_FLASH_TEST_SIZE 0U
#endif
/* No TH driver is selected: the actual module model has not been supplied.
 * LCD also needs a verified controller driver; see docs/week-03-setup.md. */
#endif
