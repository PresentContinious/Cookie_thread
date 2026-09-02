/*
 * Deep-sleep floor diagnostic.
 *
 * Does nothing but sleep forever. With CONFIG_PM the kernel idle thread puts
 * the nRF52840 into System ON deep sleep. No OpenThread, no sensors, no radio.
 * The current drawn from the supply in this state is the board + MCU sleep
 * floor under ideal firmware, the reference for the SED sleep-current
 * measurement.
 */

#include <zephyr/kernel.h>

int main(void)
{
	while (true) {
		k_sleep(K_SECONDS(3600));
	}
	return 0;
}
