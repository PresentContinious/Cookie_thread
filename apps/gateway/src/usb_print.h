#ifndef GATEWAY_USB_PRINT_H_
#define GATEWAY_USB_PRINT_H_

#include <stddef.h>
#include <stdint.h>

void gateway_usb_print_init(void);

/**
 * Emit one JSON line on the host-facing console: payload bytes verbatim
 * followed by '\n'. Safe to call from any thread; output is serialised
 * by the underlying console backend.
 */
void gateway_usb_print_line(const uint8_t *payload, size_t len);

#endif /* GATEWAY_USB_PRINT_H_ */
