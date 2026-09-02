/*
 * Contiki-NG port of the Cookie sensor node (cross-OS energy comparison).
 *
 * One reporting cycle: etimer fires -> read SHTC3 (temperature, humidity) ->
 * build the same JSON frame the Zephyr and RIOT nodes emit -> CoAP NON POST it
 * to "sensors/data" over uIP 6LoWPAN -> wait for the next cycle. The wire
 * format is identical (plus an additive "os":"contiki-ng" tag) so the
 * OS-agnostic PC tool ingests these frames unchanged.
 *
 * This is the minimal representative node for the comparison: sensor read +
 * radio transmit + idle, the energy-relevant workload Chapter 6 measures
 * across operating systems. It does not join the Zephyr Thread mesh.
 *
 * FIRST CUT — written without a local toolchain. The two spots to verify in
 * the first Linux build are (1) the SHTC3 I2C transfer against the nrf52840
 * platform's I2C/nrfx API, and (2) the exact CoAP serialise/send entry points
 * for the pinned Contiki-NG release. The process structure, the SHTC3 command
 * sequence, and the wire contract are the stable parts.
 */

#include "contiki.h"
#include "contiki-net.h"
#include "net/ipv6/uip-ds6.h"
#include "net/netstack.h"
#include "net/routing/routing.h"
#include "coap.h"
#include "coap-engine.h"
#include "coap-endpoint.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define LOG_MODULE "cookie"
#define LOG_LEVEL  LOG_LEVEL_INFO

#ifndef REPORT_INTERVAL_SEC
#define REPORT_INTERVAL_SEC   (30U)
#endif
#ifndef NODE_OS_TAG
#define NODE_OS_TAG           "contiki-ng"
#endif
#ifndef NODE_ROLE_TAG
#define NODE_ROLE_TAG         "SED"
#endif

/* Collector / border-router CoAP endpoint. The transmit (what the measurement
 * captures) happens regardless of whether a collector answers; point this at
 * the real collector address for end-to-end delivery. */
#define REPORT_EP             "coap://[fd00::1]:5683"
#define REPORT_PATH           "sensors/data"

/* Padding bytes appended to the JSON frame ("pad" field) so the CoAP payload
 * grows and 6LoWPAN fragments it — the packet-size sweep of the Chapter 6
 * campaign. Set per build: make PAYLOAD_PAD=256. */
#ifndef PAYLOAD_PAD
#define PAYLOAD_PAD           (0U)
#endif

/* Phase markers on the LED pads, same semantics as the Zephyr and RIOT ports:
 * red (P1.10) HIGH from wake until the transmit call returns (width = the OS
 * wake-to-transmit latency, rising edge = scope trigger); green (P1.15) HIGH
 * across the sensor reads. Disable with MEAS_MARKERS=0 for the cleanest
 * pure-current image. */
#ifndef MEAS_MARKERS
#define MEAS_MARKERS          (1)
#endif

/* Radio duty-cycling: power the radio down between reporting cycles so the
 * node deep-sleeps like the Zephyr Thread SED, and the wake-from-deep-sleep
 * to transmit-complete time (Gabriel's point 4) is measured on the same
 * footing on all three OSes. The node first joins the RPL DAG with the radio
 * on, then starts cycling. RADIO_SLEEP=0 keeps the CSMA default (radio always
 * on) — that build is the RX-current image. */
#ifndef RADIO_SLEEP
#define RADIO_SLEEP           (1)
#endif

/* CSMA transmits (and retries) from ctimers after coap_sendto returns; the
 * radio stays up this long after the marker falls so the fragments (up to 9
 * on pad512) actually leave before it powers down. */
#define RADIO_LINGER_MS       (300U)

/* Give RPL this long to pick a parent before duty-cycling starts anyway (the
 * transmit itself is measurable without a DAG). */
#define JOIN_TIMEOUT_SEC      (120U)

/* --- SHTC3 over software (bit-banged) I2C ----------------------------------
 * Contiki-NG's nrf52840 platform ships no I2C driver, so the SHTC3 is read
 * over a small bit-banged I2C on the Cookie pins (SCL P1.08 = abs 40,
 * SDA P0.11 = abs 11) via the Nordic nrf_gpio HAL the platform already uses.
 * Open-drain is emulated: drive low = output 0; release = input with pull-up,
 * the bus pull-ups take the line high.
 * SHTC3 @0x70: wake 0x3517 -> measure 0x7866 (T first, no stretch) -> ~15 ms ->
 * read 6 bytes -> sleep 0xB098. T = -45 + 175*raw/65535 ; RH = 100*raw/65535. */
#include "nrf_gpio.h"

#define I2C_SCL   40        /* P1.08 (port 1, pin 8) */
#define I2C_SDA   11        /* P0.11 */
#define SHTC3_ADDR 0x70
#define I2C_DLY()  clock_delay_usec(4)   /* ~100-150 kHz half-bit */

/* --- GPIO supply rail (UICR REGOUT0) ----------------------------------------
 * The Cookie is powered from USB-C through VDDH, so the nRF52840 runs in
 * high-voltage mode and the GPIO rail follows UICR REGOUT0, which a
 * chip-erase flash resets to 1.8 V — too low for the green LED and marginal
 * for the red one, and it halves every marker amplitude on the scope.
 * Restore 3.0 V exactly like the Zephyr board init does: written once to
 * UICR, then one reset applies it; later boots find it set and skip this. */
static void cookie_regout0_fix(void)
{
    if (((NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk)
             == (POWER_MAINREGSTATUS_MAINREGSTATUS_High
                 << POWER_MAINREGSTATUS_MAINREGSTATUS_Pos)) &&
       ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk)
             == (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos))) {

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

        NRF_UICR->REGOUT0 =
            (NRF_UICR->REGOUT0 & ~((uint32_t)UICR_REGOUT0_VOUT_Msk)) |
            (UICR_REGOUT0_VOUT_3V0 << UICR_REGOUT0_VOUT_Pos);

        NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
        while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}

        NVIC_SystemReset();
    }
}

static inline void scl_rel(void) { nrf_gpio_cfg_input(I2C_SCL, NRF_GPIO_PIN_PULLUP); }
static inline void scl_low(void) { nrf_gpio_pin_clear(I2C_SCL); nrf_gpio_cfg_output(I2C_SCL); }
static inline void sda_rel(void) { nrf_gpio_cfg_input(I2C_SDA, NRF_GPIO_PIN_PULLUP); }
static inline void sda_low(void) { nrf_gpio_pin_clear(I2C_SDA); nrf_gpio_cfg_output(I2C_SDA); }
static inline int  sda_get(void) { return nrf_gpio_pin_read(I2C_SDA); }

static void i2c_start(void) { sda_rel(); scl_rel(); I2C_DLY(); sda_low(); I2C_DLY(); scl_low(); I2C_DLY(); }
static void i2c_stop(void)  { sda_low(); I2C_DLY(); scl_rel(); I2C_DLY(); sda_rel(); I2C_DLY(); }

/* Returns 0 if the byte was ACKed. */
static int i2c_write(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        if (b & 0x80) { sda_rel(); } else { sda_low(); }
        I2C_DLY(); scl_rel(); I2C_DLY(); scl_low(); I2C_DLY();
        b <<= 1;
    }
    sda_rel(); I2C_DLY(); scl_rel(); I2C_DLY();
    int ack = sda_get();
    scl_low(); I2C_DLY();
    return ack ? -1 : 0;
}

static uint8_t i2c_read(int send_nack)
{
    uint8_t b = 0;
    sda_rel();
    for (int i = 0; i < 8; i++) {
        I2C_DLY(); scl_rel(); I2C_DLY();
        b = (uint8_t)((b << 1) | (sda_get() ? 1 : 0));
        scl_low(); I2C_DLY();
    }
    if (send_nack) { sda_rel(); } else { sda_low(); }
    I2C_DLY(); scl_rel(); I2C_DLY(); scl_low(); I2C_DLY();
    sda_rel();
    return b;
}

static void shtc3_i2c_init(void) { scl_rel(); sda_rel(); }

/* --- Phase markers ---------------------------------------------------------*/
#if MEAS_MARKERS
#define MARK_TX_PIN    NRF_GPIO_PIN_MAP(1, 10)   /* red   D2 */
#define MARK_SENS_PIN  NRF_GPIO_PIN_MAP(1, 15)   /* green D3 */
static void marks_init(void)
{
    nrf_gpio_pin_clear(MARK_TX_PIN);
    nrf_gpio_cfg_output(MARK_TX_PIN);
    nrf_gpio_pin_clear(MARK_SENS_PIN);
    nrf_gpio_cfg_output(MARK_SENS_PIN);
}
#define MARK_TX(on)    do { if (on) nrf_gpio_pin_set(MARK_TX_PIN); \
                            else    nrf_gpio_pin_clear(MARK_TX_PIN); } while (0)
#define MARK_SENS(on)  do { if (on) nrf_gpio_pin_set(MARK_SENS_PIN); \
                            else    nrf_gpio_pin_clear(MARK_SENS_PIN); } while (0)
#else
static void marks_init(void) { }
#define MARK_TX(on)    do { } while (0)
#define MARK_SENS(on)  do { } while (0)
#endif

/* --- ICM-20648 accelerometer, minimal raw read over the same bit-banged bus.
 * Wake it out of power-on sleep once (bank 0: REG_BANK_SEL 0x7F = 0,
 * PWR_MGMT_1 0x06 = 0x01), then read the six ACCEL_*OUT registers (0x2D..)
 * per cycle. Default full-scale +/-2 g = 16384 LSB/g; the energy cost, not
 * the exact scale, is what the measurement needs. */
#define ICM_ADDR   0x68

static int icm_write_reg(uint8_t reg, uint8_t val)
{
    i2c_start();
    if (i2c_write(ICM_ADDR << 1) != 0) { i2c_stop(); return -1; }
    i2c_write(reg);
    i2c_write(val);
    i2c_stop();
    return 0;
}

static int icm_read_regs(uint8_t reg, uint8_t *d, int n)
{
    i2c_start();
    if (i2c_write(ICM_ADDR << 1) != 0) { i2c_stop(); return -1; }
    i2c_write(reg);
    i2c_start();
    if (i2c_write((ICM_ADDR << 1) | 1) != 0) { i2c_stop(); return -1; }
    for (int i = 0; i < n; i++) {
        d[i] = i2c_read(i == n - 1);
    }
    i2c_stop();
    return 0;
}

static bool icm_ok;

static void icm_init(void)
{
    if (icm_write_reg(0x7F, 0x00) != 0) {   /* bank 0 */
        return;
    }
    if (icm_write_reg(0x06, 0x01) != 0) {   /* wake, auto clock */
        return;
    }
    clock_delay_usec(2000);
    icm_ok = true;
}

static int icm_read_accel(float a[3])
{
    uint8_t d[6];
    if (!icm_ok || icm_read_regs(0x2D, d, 6) != 0) {
        return -1;
    }
    /* Re-assert PWR_MGMT_1 SLEEP (0x41 = SLEEP | CLK_AUTO) so the ICM-20648
     * idles at ~8 uA between cycles instead of free-running ~4 mA. Without this
     * the IMU, woken once in icm_init(), dominates the whole sleep floor. This
     * mirrors the Zephyr port (cookie_icm20648_sleep). The device still ACKs the
     * I2C read while asleep, so the accelerometer sample is best-effort. */
    (void)icm_write_reg(0x06, 0x41);
    for (int i = 0; i < 3; i++) {
        int16_t raw = (int16_t)(((uint16_t)d[2 * i] << 8) | d[2 * i + 1]);
        a[i] = raw / 16384.0f;
    }
    return 0;
}

static int shtc3_read(float *temp_c, float *humid_pct)
{
    /* wake */
    i2c_start();
    if (i2c_write(SHTC3_ADDR << 1) != 0) { i2c_stop(); return -1; }
    i2c_write(0x35); i2c_write(0x17);
    i2c_stop();
    clock_delay_usec(500);

    /* measure, temperature first, no clock stretching (0x7866) */
    i2c_start();
    i2c_write(SHTC3_ADDR << 1); i2c_write(0x78); i2c_write(0x66);
    i2c_stop();
    clock_delay_usec(15000);   /* conversion time ~12 ms */

    /* read 6 bytes: T_msb T_lsb crc  RH_msb RH_lsb crc */
    i2c_start();
    if (i2c_write((SHTC3_ADDR << 1) | 1) != 0) { i2c_stop(); return -1; }
    uint8_t th = i2c_read(0), tl = i2c_read(0), tcrc = i2c_read(0);
    uint8_t hh = i2c_read(0), hl = i2c_read(0), hcrc = i2c_read(1);
    i2c_stop();
    (void)tcrc; (void)hcrc;

    /* sleep (~0.7 uA) */
    i2c_start();
    i2c_write(SHTC3_ADDR << 1); i2c_write(0xB0); i2c_write(0x98);
    i2c_stop();

    uint16_t raw_t = ((uint16_t)th << 8) | tl;
    uint16_t raw_h = ((uint16_t)hh << 8) | hl;
    *temp_c    = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    *humid_pct =          100.0f * ((float)raw_h / 65535.0f);
    return 0;
}

/* --- JSON frame, identical schema to the Zephyr/RIOT ports ----------------- */
static int build_frame(char *buf, size_t len, unsigned ts_s,
                       bool have_th, float temp_c, float humid_pct,
                       bool have_accel, const float accel_g[3])
{
    /* src: last 4 hex chars of the link-layer address. */
    char src[5] = "0000";
    if (uip_ds6_get_link_local(ADDR_PREFERRED) != NULL) {
        const uip_lladdr_t *ll = &uip_lladdr;
        snprintf(src, sizeof(src), "%02x%02x",
                 ll->addr[LINKADDR_SIZE - 2], ll->addr[LINKADDR_SIZE - 1]);
    }

    int off = snprintf(buf, len,
        "{\"ts\":%u,\"src\":\"%s\",\"role\":\"%s\",\"rssi\":0,\"hops\":0",
        ts_s * 1000U, src, NODE_ROLE_TAG);
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
        int an = snprintf(buf + off, len - off,
            ",\"accel_g\":[%.3f,%.3f,%.3f]",
            (double)accel_g[0], (double)accel_g[1], (double)accel_g[2]);
        if (an < 0 || (size_t)(off + an) >= len) {
            return -1;
        }
        off += an;
    }
    int n = snprintf(buf + off, len - off, ",\"os\":\"%s\"", NODE_OS_TAG);
    if (n < 0 || (size_t)(off + n) >= len) {
        return -1;
    }
    off += n;
#if PAYLOAD_PAD > 0
    /* Packet-size sweep filler, same "pad" key as the other ports. */
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

/* --- Process ---------------------------------------------------------------*/
PROCESS(cookie_sensor_node, "Cookie sensor node (Contiki-NG)");
AUTOSTART_PROCESSES(&cookie_sensor_node);

static coap_endpoint_t collector_ep;
static char json[192 + PAYLOAD_PAD];

PROCESS_THREAD(cookie_sensor_node, ev, data)
{
    static struct etimer cycle;

    PROCESS_BEGIN();

    cookie_regout0_fix();

    printf("cookie sensor_node (Contiki-NG): " NODE_OS_TAG " role=" NODE_ROLE_TAG "\n");

    shtc3_i2c_init();
    marks_init();
    icm_init();

#if MEAS_MARKERS
    /* Three green blinks at boot: the image is alive (same convention as the
     * Zephyr and RIOT measurement builds). Busy-wait is fine this once. */
    for (int bi = 0; bi < 3; bi++) {
        nrf_gpio_pin_set(MARK_SENS_PIN);
        clock_delay_usec(60000); clock_delay_usec(60000);
        nrf_gpio_pin_clear(MARK_SENS_PIN);
        clock_delay_usec(60000); clock_delay_usec(60000);
    }
#endif

    coap_endpoint_parse(REPORT_EP, strlen(REPORT_EP), &collector_ep);

#if RADIO_SLEEP
    /* Join first with the radio on: RPL has to hear DIOs to pick a parent.
     * Then power the radio down and start the duty cycle. */
    {
        static struct etimer join;
        static unsigned waited;
        waited = 0;
        while (!NETSTACK_ROUTING.node_is_reachable()
               && waited < JOIN_TIMEOUT_SEC) {
            etimer_set(&join, CLOCK_SECOND);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&join));
            waited++;
        }
        printf("cookie: %s after %u s\n",
               NETSTACK_ROUTING.node_is_reachable() ? "joined DAG"
                                                    : "join timeout",
               waited);
        NETSTACK_MAC.off();
    }
#endif

    etimer_set(&cycle, CLOCK_SECOND * REPORT_INTERVAL_SEC);

    while (1) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&cycle));

        /* Rising edge = scope trigger; high until the transmit call
         * returns (the wake-to-transmit latency of the campaign). The radio
         * wake sits inside the window by design: the metric is "hardware
         * wakes -> frame handed to a transmit-capable stack". */
        MARK_TX(1);
#if RADIO_SLEEP
        NETSTACK_MAC.on();
#endif
        MARK_SENS(1);

        bool have_th = false;
        float t = 0.0f, h = 0.0f;
        if (shtc3_read(&t, &h) == 0) {
            have_th = true;
        }

        bool have_accel = false;
        static float accel_g[3];
        if (icm_read_accel(accel_g) == 0) {
            have_accel = true;
        }

        MARK_SENS(0);

        int n = build_frame(json, sizeof(json),
                            (unsigned)clock_seconds(), have_th, t, h,
                            have_accel, accel_g);
        if (n > 0) {
            printf("%s\n", json);   /* local telemetry, like the other ports */

            static coap_message_t req[1];
            coap_init_message(req, COAP_TYPE_NON, COAP_POST, coap_get_mid());
            coap_set_header_uri_path(req, REPORT_PATH);
            coap_set_header_content_format(req, APPLICATION_JSON);
            coap_set_payload(req, (uint8_t *)json, (size_t)n);

            static uint8_t txbuf[COAP_MAX_CHUNK_SIZE + 64];
            size_t txlen = coap_serialize_message(req, txbuf);
            /* coap_sendto returns <0 (before any radio activity) whenever the
             * node is not DAG-reachable: the red marker would still pulse but
             * NOTHING was transmitted. Count and print the drops so such
             * captures are never mistaken for TX energy. */
            static unsigned tx_drop;
            if (coap_sendto(&collector_ep, txbuf, txlen) < 0) {
                tx_drop++;
                printf("tx_drop n=%u (not joined?)\n", tx_drop);
            }
        }

        /* Falling edge: transmit handed off. */
        MARK_TX(0);

#if RADIO_SLEEP
        /* Let CSMA push the queued fragments out, then power the radio down
         * for the rest of the cycle — the deep-sleep phase. */
        {
            static struct etimer linger;
            etimer_set(&linger, (CLOCK_SECOND * RADIO_LINGER_MS) / 1000);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&linger));
            NETSTACK_MAC.off();

            /* Re-assert radio-off every 2 s until the next cycle: the node's
             * own RPL control traffic (DAO refresh, probing) transmits through
             * the MAC regardless of the off state and leaves the radio latched
             * in RX for seconds afterwards. A 2 s etimer wake costs
             * microseconds of CPU against seconds of RX current. */
            static struct etimer reoff;
            while (!etimer_expired(&cycle)) {
                etimer_set(&reoff, CLOCK_SECOND * 2);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&reoff)
                                         || etimer_expired(&cycle));
                NETSTACK_MAC.off();
            }
        }
#endif

        etimer_reset(&cycle);
    }

    PROCESS_END();
}
