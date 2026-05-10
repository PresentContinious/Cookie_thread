/*
 * SYS_INIT stage tracer.
 *
 * Drives Dongle LEDs via direct nRF52 GPIO registers (no Zephyr GPIO API
 * required so we can run from PRE_KERNEL_1 onwards). LEDs on the Dongle
 * are ACTIVE_LOW; OUTCLR turns them ON, OUTSET turns them OFF.
 *
 *   green = P0.06   (Dongle LED1, the dedicated green LED)
 *   red   = P0.08   (Dongle LED2 RGB red component)
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <hal/nrf_gpio.h>

#define LED_GREEN_PIN  NRF_GPIO_PIN_MAP(0, 6)
#define LED_RED_PIN    NRF_GPIO_PIN_MAP(0, 8)

static inline void led_init_raw(void)
{
	nrf_gpio_cfg_output(LED_GREEN_PIN);
	nrf_gpio_cfg_output(LED_RED_PIN);
	nrf_gpio_pin_set(LED_GREEN_PIN);  /* OFF (active low) */
	nrf_gpio_pin_set(LED_RED_PIN);
}

static inline void led_green(int on)
{
	if (on) nrf_gpio_pin_clear(LED_GREEN_PIN);
	else    nrf_gpio_pin_set(LED_GREEN_PIN);
}

static inline void led_red(int on)
{
	if (on) nrf_gpio_pin_clear(LED_RED_PIN);
	else    nrf_gpio_pin_set(LED_RED_PIN);
}

/* Busy-wait usable from any init level — ~64 MHz core, ~4 cycles per
 * iteration; 16 M iterations ~= 1 second. */
static void busy_wait_ms(uint32_t ms)
{
	volatile uint32_t i;
	uint32_t total = 16000UL * ms;
	for (i = 0; i < total; i++) {
		__asm__ volatile ("nop");
	}
}

/* ------------------- SYS_INIT stage markers ------------------- */

static int trace_pre_kernel_1(void)
{
	led_init_raw();
	led_green(1);
	busy_wait_ms(200);
	led_green(0);
	return 0;
}
SYS_INIT(trace_pre_kernel_1, PRE_KERNEL_1, 0);

static int trace_pre_kernel_2(void)
{
	led_red(1);
	busy_wait_ms(200);
	led_red(0);
	return 0;
}
SYS_INIT(trace_pre_kernel_2, PRE_KERNEL_2, 99);

static int trace_post_kernel(void)
{
	led_green(1); led_red(1);
	busy_wait_ms(200);
	led_green(0); led_red(0);
	return 0;
}
SYS_INIT(trace_post_kernel, POST_KERNEL, 99);

static int trace_application(void)
{
	for (int i = 0; i < 6; i++) {
		led_green(1); led_red(0);
		busy_wait_ms(33);
		led_green(0); led_red(1);
		busy_wait_ms(33);
	}
	led_green(0); led_red(0);
	return 0;
}
SYS_INIT(trace_application, APPLICATION, 99);

/* ------------------- main ------------------- */

int main(void)
{
	led_green(1); led_red(1);
	k_sleep(K_MSEC(1000));
	led_green(0); led_red(0);
	k_sleep(K_MSEC(300));

	while (1) {
		led_red(1);   k_sleep(K_MSEC(125));
		led_red(0);   led_green(1);
		k_sleep(K_MSEC(125));
		led_green(0);
	}
	return 0;
}
