#ifndef COOKIE_THREAD_SETUP_H_
#define COOKIE_THREAD_SETUP_H_

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * Install operational dataset from Kconfig credentials, configure profile-
 * specific link mode (rxOnWhenIdle = false for SED), and start OpenThread.
 *
 * @return 0 on success, negative errno otherwise.
 */
int cookie_thread_start(void);

/**
 * Block until the local OpenThread role reaches at least CHILD, or the
 * timeout expires.
 *
 * @return 0 if attached, -ETIMEDOUT otherwise.
 */
int cookie_thread_wait_attached(k_timeout_t timeout);

/**
 * Read the runtime Thread role as a string ("LEADER", "ROUTER", "REED",
 * "CHILD", "DETACHED"). Pointer is valid forever (static literals).
 */
const char *cookie_thread_role_str(void);

/**
 * Fill src[5] with the last four hex chars of the EUI-64 plus null.
 */
void cookie_thread_format_src(char src_out[5]);

/**
 * @return RSSI to parent in dBm, or 0 if there is no parent
 *         (Leader, REED with no parent slot, or detached).
 */
int8_t cookie_thread_parent_rssi(void);

/**
 * @return number of hops from this node to the Leader, or 0 if self is
 *         the Leader / unattached.
 */
uint8_t cookie_thread_hops_to_leader(void);

#endif /* COOKIE_THREAD_SETUP_H_ */
