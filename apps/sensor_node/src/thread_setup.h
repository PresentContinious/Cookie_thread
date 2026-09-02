#ifndef COOKIE_THREAD_SETUP_H_
#define COOKIE_THREAD_SETUP_H_

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * Block until the local OpenThread role reaches at least CHILD, or until
 * the timeout expires. OT auto-starts in SYS_INIT, so by the time main()
 * runs the interface is up and attempting to attach.
 *
 * @return 0 if attached, -ETIMEDOUT otherwise.
 */
int cookie_thread_wait_attached(k_timeout_t timeout);

/**
 * Read the runtime Thread role as a string ("LEADER", "ROUTER", "CHILD",
 * "DETACHED", "DISABLED", "SED" for SED-profile builds).
 */
const char *cookie_thread_role_str(void);

/**
 * Fill src[5] with the last four hex chars of the EUI-64 plus null.
 */
void cookie_thread_format_src(char src_out[5]);

/**
 * @return RSSI to parent in dBm, or 0 if there is no parent.
 */
int8_t cookie_thread_parent_rssi(void);

/**
 * @return number of hops to the Leader (coarse estimate).
 */
uint8_t cookie_thread_hops_to_leader(void);

#endif /* COOKIE_THREAD_SETUP_H_ */
