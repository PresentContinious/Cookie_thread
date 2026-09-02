#ifndef COOKIE_NODE_LOOP_H_
#define COOKIE_NODE_LOOP_H_

/**
 * Start the AUTO-profile main loop: periodic timer fires every
 * CONFIG_NODE_REPORT_INTERVAL_SEC, samples sensors, pushes a frame.
 *
 * Returns immediately; the timer + workqueue handlers keep running on
 * Zephyr's system threads.
 */
int cookie_node_loop_start(void);

#endif /* COOKIE_NODE_LOOP_H_ */
