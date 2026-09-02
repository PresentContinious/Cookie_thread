#ifndef COOKIE_SED_LOOP_H_
#define COOKIE_SED_LOOP_H_

/**
 * Run the SED main loop: sleep, wake, sample, push, quiesce, repeat.
 * Never returns under normal operation.
 */
int cookie_sed_loop_run(void);

#endif /* COOKIE_SED_LOOP_H_ */
