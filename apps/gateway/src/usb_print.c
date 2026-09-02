/*
 * Raw JSON-line emission on the host console (USB-CDC on Dongle).
 *
 * #2a stub: payload bytes verbatim + '\n'. Log lines from CONFIG_LOG
 * share the same console; the PC tool discriminates by leading '{' for
 * frames vs '[' for log timestamps.
 */

#include "usb_print.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static K_MUTEX_DEFINE(print_lock);

void gateway_usb_print_init(void)
{
	/* Console is brought up by the Dongle defconfig (USB-CDC ACM).
	 * Nothing extra to do here; init exists so future tweaks (line
	 * mode, locked output buffer, etc.) have a clear home. */
}

void gateway_usb_print_line(const uint8_t *payload, size_t len)
{
	k_mutex_lock(&print_lock, K_FOREVER);
	for (size_t i = 0; i < len; i++) {
		printk("%c", payload[i]);
	}
	printk("\n");
	k_mutex_unlock(&print_lock);
}
