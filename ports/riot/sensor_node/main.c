/*
 * RIOT port of the Cookie sensor node (cross-OS energy comparison).
 *
 * One reporting cycle: wake -> read SHTC3 (temperature, humidity) -> build the
 * same JSON frame the Zephyr node emits -> CoAP POST it to "sensors/data" over
 * GNRC 6LoWPAN -> sleep until the next cycle. The wire format is identical to
 * apps/sensor_node so the OS-agnostic PC tool ingests RIOT frames unchanged.
 *
 * This is the minimal representative node for the comparison, not a full mesh:
 * it carries the energy-relevant workload (sensor read + radio transmit +
 * sleep), which is what the Chapter 6 measurement compares across operating
 * systems. IMU and the INA333 self-current path are optional add-ons left for
 * a later pass; they are not needed to characterise the duty cycle.
 *
 * FIRST CUT — written without a local toolchain. Expect to compile-and-fix the
 * RIOT-API specifics (gcoap_req_send signature, shtcx field units) in the
 * first Linux build. The structure and the wire contract are the stable parts.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "board.h"
#include "fmt.h"
#include "ztimer.h"
#include "net/gcoap.h"
#include "net/ipv6/addr.h"
#include "net/gnrc/netif.h"
#include "net/gnrc/netapi.h"
#include "net/netopt.h"
#include "periph/i2c.h"

#include "shtcx.h"
#include "shtcx_params.h"

#ifndef REPORT_INTERVAL_SEC
#define REPORT_INTERVAL_SEC   (30U)
#endif

/* Padding bytes appended to the JSON frame ("pad" field) so the CoAP payload
 * grows and 6LoWPAN fragments it — the packet-size sweep of the Chapter 6
 * campaign. Set per build: make PAYLOAD_PAD=256. */
#ifndef PAYLOAD_PAD
#define PAYLOAD_PAD           (0U)
#endif

/* Phase markers on the LED pads, same semantics as the Zephyr and Contiki-NG
 * ports: red (LED0, P1.10) HIGH from wake until the transmit call returns
 * (width = the OS wake-to-transmit latency, rising edge = scope trigger);
 * green (LED1, P1.15) HIGH across the sensor reads. Disable with
 * MEAS_MARKERS=0 for the cleanest pure-current image. */
#ifndef MEAS_MARKERS
#define MEAS_MARKERS          (1)
#endif

#if MEAS_MARKERS && defined(LED0_PIN)
#define MARK_TX_ON()          LED0_ON
#define MARK_TX_OFF()         LED0_OFF
#define MARK_SENS_ON()        LED1_ON
#define MARK_SENS_OFF()       LED1_OFF
#else
#define MARK_TX_ON()          do { } while (0)
#define MARK_TX_OFF()         do { } while (0)
#define MARK_SENS_ON()        do { } while (0)
#define MARK_SENS_OFF()       do { } while (0)
#endif

/* Radio duty-cycling: put the 802.15.4 radio to sleep between reporting
 * cycles so the node deep-sleeps like the Zephyr Thread SED, and the
 * wake-from-deep-sleep-to-transmit time (Gabriel's point 4) is measured on
 * the same footing on all three OSes. RADIO_SLEEP=0 keeps the GNRC default
 * (radio always in RX) — that build is the RX-current image. */
#ifndef RADIO_SLEEP
#define RADIO_SLEEP           (1)
#endif

/* GNRC transmits from the netif thread after gcoap_req_send returns; the
 * radio stays up this long after the marker falls so the fragments (up to 9
 * on pad512) actually leave before the sleep state is set. */
#define RADIO_LINGER_MS       (300U)

#ifndef NODE_OS_TAG
#define NODE_OS_TAG           "riot"
#endif

#ifndef NODE_ROLE_TAG
/* The standalone RIOT node has no Thread role; it reports the operating mode
 * it is configured for so the comparison tables line up with the Zephyr SED
 * and AUTO profiles. */
#define NODE_ROLE_TAG         "SED"
#endif

/* Where reports go. ff02::1 is the link-local all-nodes group: the transmit
 * always happens (so the radio energy is real for the measurement) whether or
 * not a collector is listening. A unicast gateway address can be set here once
 * a RIOT border router is in the mesh. */
#define REPORT_DST            "ff02::1"
#define REPORT_PORT           (5683U)
/* Leading slash is mandatory: gcoap_req_init asserts path[0] == '/' (and
 * DEVELHELP=1 turns that assert into a panic loop on the first post). */
#define REPORT_PATH           "/sensors/data"

static shtcx_t shtc3;
static bool shtc3_ok;

/* Last 4 hex chars of the radio EUI-64, matching the Zephyr "src" field. */
static char node_src[5] = "0000";

/* The 6LoWPAN interface pid. A link-local destination (the ff02::1 report
 * group) needs the egress interface set explicitly on the UDP endpoint,
 * otherwise the send fails silently. */
static kernel_pid_t netif_pid = KERNEL_PID_UNDEF;

static void derive_src(void)
{
    gnrc_netif_t *netif = gnrc_netif_iter(NULL);
    if (netif == NULL) {
        return;
    }
    netif_pid = netif->pid;
    uint8_t l2[GNRC_NETIF_L2ADDR_MAXLEN];
    int len = gnrc_netapi_get(netif->pid, NETOPT_ADDRESS_LONG, 0,
                              l2, sizeof(l2));
    if (len >= 2) {
        fmt_byte_hex(&node_src[0], l2[len - 2]);
        fmt_byte_hex(&node_src[2], l2[len - 1]);
        node_src[4] = '\0';
    }
}

#if RADIO_SLEEP
/* Returns 0 when the netif accepted the state (or already was in it).
 * The submac path only supports SLEEP and IDLE: STANDBY is always -ENOTSUP,
 * so there is no fallback state. -EALREADY is success, not failure. */
static int radio_set_state(netopt_state_t st)
{
    if (netif_pid == KERNEL_PID_UNDEF) {
        return -ENODEV;
    }
    int res = gnrc_netapi_set(netif_pid, NETOPT_STATE, 0, &st, sizeof(st));
    if (res == -EALREADY) {
        return 0;
    }
    return (res < 0) ? res : 0;
}

static void radio_sleep(void)
{
    int res = radio_set_state(NETOPT_STATE_SLEEP);
    if (res == -EBUSY) {
        /* A transmission is still in flight: give it one more linger and
         * retry, otherwise the radio silently stays in RX all cycle. */
        ztimer_sleep(ZTIMER_MSEC, 50);
        res = radio_set_state(NETOPT_STATE_SLEEP);
    }
    if (res != 0) {
        puts("radio sleep rejected by driver");
    }
}

static void radio_wake(void)
{
    (void)radio_set_state(NETOPT_STATE_IDLE);
}
#else
#define radio_sleep()   do { } while (0)
#define radio_wake()    do { } while (0)
#endif

/* --- SHTC3 wake/sleep --------------------------------------------------------
 * RIOT's shtcx driver was written for the SHTC1, which has no sleep mode: it
 * never issues the SHTC3 wake-up command. The SHTC3 powers up asleep (and the
 * Zephyr driver also leaves it asleep), so without an explicit wake-up every
 * driver access is NACKed and init fails. Wake it around each access and put
 * it back to sleep afterwards (~0.7 uA), matching the Zephyr node's floor. */
#define SHTC3_I2C_ADDR        (0x70)

static void shtc3_wakeup(void)
{
    uint8_t cmd[2] = { 0x35, 0x17 };
    i2c_acquire(I2C_DEV(0));
    (void)i2c_write_bytes(I2C_DEV(0), SHTC3_I2C_ADDR, cmd, sizeof(cmd), 0);
    i2c_release(I2C_DEV(0));
    ztimer_sleep(ZTIMER_MSEC, 1);   /* datasheet wake-up time: 240 us */
}

static void shtc3_to_sleep(void)
{
    uint8_t cmd[2] = { 0xB0, 0x98 };
    i2c_acquire(I2C_DEV(0));
    (void)i2c_write_bytes(I2C_DEV(0), SHTC3_I2C_ADDR, cmd, sizeof(cmd), 0);
    i2c_release(I2C_DEV(0));
}

/* --- ICM-20648 accelerometer, minimal raw read ------------------------------
 * RIOT ships no ICM-20648 driver, so the accelerometer is read directly over
 * periph_i2c (same TWIM bus as the SHTC3). Enough for the campaign's
 * "current while reading the accelerometer" point: wake the device out of its
 * power-on sleep once, then read the six ACCEL_*OUT registers per cycle.
 * Bank 0: REG_BANK_SEL 0x7F, PWR_MGMT_1 0x06, ACCEL_XOUT_H 0x2D. Default
 * full-scale (+/-2 g, 16384 LSB/g) is kept — the energy cost, not the exact
 * scale, is what the measurement needs. */
#define ICM_ADDR              (0x68)
#define ICM_REG_BANK_SEL      (0x7F)
#define ICM_PWR_MGMT_1        (0x06)
#define ICM_ACCEL_XOUT_H      (0x2D)

static bool icm_ok;

static void icm_init(void)
{
    i2c_acquire(I2C_DEV(0));
    int rc = i2c_write_reg(I2C_DEV(0), ICM_ADDR, ICM_REG_BANK_SEL, 0x00, 0);
    if (rc == 0) {
        /* Clear the sleep bit, auto clock select. */
        rc = i2c_write_reg(I2C_DEV(0), ICM_ADDR, ICM_PWR_MGMT_1, 0x01, 0);
    }
    i2c_release(I2C_DEV(0));
    icm_ok = (rc == 0);
}

static int icm_read_accel(float a[3])
{
    uint8_t d[6];

    i2c_acquire(I2C_DEV(0));
    int rc = i2c_read_regs(I2C_DEV(0), ICM_ADDR, ICM_ACCEL_XOUT_H,
                           d, sizeof(d), 0);
    /* Re-assert PWR_MGMT_1 SLEEP (0x41 = SLEEP | CLK_AUTO) so the ICM-20648
     * idles at ~8 uA between cycles instead of free-running ~4 mA. Without this
     * the IMU, woken once in icm_init(), dominates the whole sleep floor. This
     * mirrors the Zephyr port (cookie_icm20648_sleep). The device still ACKs the
     * I2C read while asleep, so the accelerometer sample is best-effort. */
    (void)i2c_write_reg(I2C_DEV(0), ICM_ADDR, ICM_PWR_MGMT_1, 0x41, 0);
    i2c_release(I2C_DEV(0));
    if (rc != 0) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        int16_t raw = (int16_t)(((uint16_t)d[2 * i] << 8) | d[2 * i + 1]);
        a[i] = raw / 16384.0f;
    }
    return 0;
}

/* Builds the single-line JSON frame. Keeps the Zephyr key order and appends an
 * additive "os" tag so the cross-OS measurement can label each sample. */
static int build_frame(char *buf, size_t len, uint32_t ts_ms,
                       uint32_t t_active_ms, bool have_th,
                       float temp_c, float humid_pct,
                       bool have_accel, const float accel_g[3])
{
    int off = snprintf(buf, len,
        "{\"ts\":%u,\"src\":\"%s\",\"role\":\"%s\",\"rssi\":0,\"hops\":0",
        (unsigned)ts_ms, node_src, NODE_ROLE_TAG);
    if (off < 0 || (size_t)off >= len) {
        return -1;
    }
    if (have_th) {
        int n = snprintf(buf + off, len - off,
            ",\"temp_c\":%.2f,\"humid_pct\":%.2f",
            (double)temp_c, (double)humid_pct);
        if (n < 0 || (size_t)(off + n) >= len) {
            return -1;
        }
        off += n;
    }
    if (have_accel) {
        int n = snprintf(buf + off, len - off,
            ",\"accel_g\":[%.3f,%.3f,%.3f]",
            (double)accel_g[0], (double)accel_g[1], (double)accel_g[2]);
        if (n < 0 || (size_t)(off + n) >= len) {
            return -1;
        }
        off += n;
    }
    int n = snprintf(buf + off, len - off,
        ",\"t_active_ms\":%u,\"os\":\"%s\"",
        (unsigned)t_active_ms, NODE_OS_TAG);
    if (n < 0 || (size_t)(off + n) >= len) {
        return -1;
    }
    off += n;
#if PAYLOAD_PAD > 0
    /* Packet-size sweep filler, same "pad" key as the Zephyr encoder. */
    n = snprintf(buf + off, len - off, ",\"pad\":\"");
    if (n < 0 || (size_t)(off + n) >= len) {
        return -1;
    }
    off += n;
    for (unsigned i = 0; i < PAYLOAD_PAD; i++) {
        if ((size_t)off >= len - 3) {
            return -1;
        }
        buf[off++] = 'x';
    }
    buf[off++] = '"';
    buf[off] = '\0';
#endif
    if ((size_t)off >= len - 2) {
        return -1;
    }
    buf[off++] = '}';
    buf[off] = '\0';
    return off;
}

static void coap_post(const char *json, size_t json_len)
{
    /* Static, not stack: at 768 bytes (pad512 sweep) this buffer overflows the
     * main-thread stack and the corrupted return address hard-faults on the
     * first transmit. Single caller, so static is safe. */
    static uint8_t buf[CONFIG_GCOAP_PDU_BUF_SIZE];
    coap_pkt_t pdu;

    gcoap_req_init(&pdu, buf, sizeof(buf), COAP_METHOD_POST, REPORT_PATH);
    coap_hdr_set_type(pdu.hdr, COAP_TYPE_NON);
    coap_opt_add_format(&pdu, COAP_FORMAT_JSON);
    ssize_t hdr_len = coap_opt_finish(&pdu, COAP_OPT_FINISH_PAYLOAD);
    if (hdr_len < 0) {
        return;
    }
    if (json_len > pdu.payload_len) {
        return;
    }
    memcpy(pdu.payload, json, json_len);
    size_t total = (size_t)hdr_len + json_len;

    sock_udp_ep_t remote;
    memset(&remote, 0, sizeof(remote));
    remote.family = AF_INET6;
    remote.port   = REPORT_PORT;
    /* Link-local destination: the egress interface is mandatory, or the
     * stack drops the send without transmitting anything. */
    remote.netif  = (uint16_t)netif_pid;
    if (ipv6_addr_from_str((ipv6_addr_t *)&remote.addr.ipv6, REPORT_DST) == NULL) {
        return;
    }
    /* NON multicast, no response handler — fire and forget, like the SED push. */
    gcoap_req_send(buf, total, &remote, NULL, NULL, NULL, GCOAP_SOCKET_TYPE_UDP);
}

int main(void)
{
    puts("cookie sensor_node (RIOT): " NODE_OS_TAG " role=" NODE_ROLE_TAG);

#if defined(LED1_PIN)
    /* Three green blinks at boot: the image is alive (the SED-style loop is
     * otherwise dark until its first cycle). Same convention as the Zephyr
     * and Contiki-NG measurement builds. */
    for (int i = 0; i < 3; i++) {
        LED1_ON;
        ztimer_sleep(ZTIMER_MSEC, 120);
        LED1_OFF;
        ztimer_sleep(ZTIMER_MSEC, 120);
    }
#endif

    derive_src();

#if RADIO_SLEEP
    /* One sleep/wake probe at boot. If the driver rejects the low-power
     * state, say so on the red LED (10 fast blinks): the bench then knows
     * a flat ~12 mA baseline is a driver limit, not a build mix-up. */
    if (radio_set_state(NETOPT_STATE_SLEEP) != 0) {
#if defined(LED0_PIN)
        for (int i = 0; i < 10; i++) {
            LED0_ON;
            ztimer_sleep(ZTIMER_MSEC, 60);
            LED0_OFF;
            ztimer_sleep(ZTIMER_MSEC, 60);
        }
#endif
    }
    (void)radio_set_state(NETOPT_STATE_IDLE);
#endif

    shtc3_wakeup();
    if (shtcx_init(&shtc3, &shtcx_params[0]) == SHTCX_OK) {
        shtc3_ok = true;
        shtc3_to_sleep();
    }
    else {
        puts("SHTC3 init failed — frames will omit temp/humid");
    }
    icm_init();
    if (!icm_ok) {
        puts("ICM-20648 init failed — frames will omit accel");
    }

    static char json[224 + PAYLOAD_PAD];

    while (1) {
        uint32_t t_start = ztimer_now(ZTIMER_MSEC);

        /* Rising edge = scope trigger; high until the transmit call
         * returns (the wake-to-transmit latency of the campaign). The radio
         * wake sits inside the window by design: the metric is "hardware
         * wakes -> frame handed to a transmit-capable stack". */
        MARK_TX_ON();
        radio_wake();
        MARK_SENS_ON();

        bool have_th = false;
        float temp_c = 0.0f, humid_pct = 0.0f;
        if (shtc3_ok) {
            /* RIOT's shtcx_read takes humidity first, then temperature, both in
             * centi-units (humidity uint16, temperature int16). */
            uint16_t hum_cc;   /* centi-%RH */
            int16_t  temp_cc;  /* centi-degC */
            shtc3_wakeup();
            if (shtcx_read(&shtc3, &hum_cc, &temp_cc) == SHTCX_OK) {
                temp_c    = temp_cc / 100.0f;
                humid_pct = hum_cc / 100.0f;
                have_th   = true;
            }
            shtc3_to_sleep();
        }

        bool have_accel = false;
        float accel_g[3] = { 0.0f, 0.0f, 0.0f };
        if (icm_ok && icm_read_accel(accel_g) == 0) {
            have_accel = true;
        }

        MARK_SENS_OFF();

        uint32_t t_active = ztimer_now(ZTIMER_MSEC) - t_start;
        int n = build_frame(json, sizeof(json), t_start, t_active,
                            have_th, temp_c, humid_pct,
                            have_accel, accel_g);
        if (n > 0) {
            puts(json);                 /* local telemetry, like the Zephyr node */
            coap_post(json, (size_t)n);
        }

        /* Falling edge: transmit handed off. */
        MARK_TX_OFF();

        /* Idle until the next cycle. With RADIO_SLEEP the radio powers down
         * after a short linger (GNRC pushes the fragments out from its own
         * thread), then the idle thread's WFI drops the CPU: that pair is
         * the deep-sleep phase of the duty cycle. */
#if RADIO_SLEEP
        ztimer_sleep(ZTIMER_MSEC, RADIO_LINGER_MS);
        radio_sleep();
        ztimer_sleep(ZTIMER_MSEC,
                     (REPORT_INTERVAL_SEC * 1000U) - RADIO_LINGER_MS);
#else
        ztimer_sleep(ZTIMER_MSEC, REPORT_INTERVAL_SEC * 1000U);
#endif
    }

    return 0;
}
