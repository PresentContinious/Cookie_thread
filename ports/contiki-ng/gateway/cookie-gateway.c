/*
 * Contiki-NG gateway/collector — receive side of the Cookie-only protocol test.
 *
 * Becomes the RPL DAG root with a stable address (fd00::1) and runs a CoAP
 * server resource at "sensors/data" that prints every frame POSTed to it. Run
 * this on one Cookie and the Contiki-NG sensor node on another: the sensor
 * joins the DAG, gets an fd00::/64 address, and POSTs its frames here. No
 * Dongle needed. The sensor node's REPORT_EP (coap://[fd00::1]:5683) matches
 * the address set up below.
 *
 * FIRST CUT — verify the RPL root-prefix call and the CoAP resource macros
 * against the pinned Contiki-NG release in the first Linux build.
 */

#include "contiki.h"
#include "contiki-net.h"
#include "net/routing/routing.h"
#include "net/ipv6/uip-ds6.h"
#include "coap-engine.h"
#include "coap.h"

#include <stdio.h>
#include <string.h>

#include "nrf_gpio.h"

/* Bench indication with no console attached (Cookie LED pads): steady green
 * = the gateway is up; red toggles on every received frame, so delivery is
 * visible on the board itself. */
#define GW_LED_RED    NRF_GPIO_PIN_MAP(1, 10)
#define GW_LED_GREEN  NRF_GPIO_PIN_MAP(1, 15)

/* The Cookie is powered from USB-C through VDDH (high-voltage mode): the GPIO
 * rail follows UICR REGOUT0, which a chip-erase flash resets to 1.8 V — the
 * green LED cannot light there and the red one is dim, so a running gateway
 * looks dead. Restore 3.0 V like the Zephyr board init: one UICR write, one
 * reset, later boots skip it. */
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

/* POST handler: print the JSON payload the sensor node sent. */
static void res_post_handler(coap_message_t *request, coap_message_t *response,
                             uint8_t *buffer, uint16_t preferred_size,
                             int32_t *offset)
{
    const uint8_t *payload = NULL;
    int len = coap_get_payload(request, &payload);
    if (len > 0) {
        printf("RX %.*s\n", len, (const char *)payload);
        nrf_gpio_pin_toggle(GW_LED_RED);
    }
    coap_set_status_code(response, CHANGED_2_04);
}

RESOURCE(res_sensors,
         "title=\"sensors\";rt=\"data\"",
         NULL,                 /* GET  */
         res_post_handler,     /* POST */
         NULL,                 /* PUT  */
         NULL);                /* DELETE */

PROCESS(cookie_gateway, "Cookie gateway (Contiki-NG)");
AUTOSTART_PROCESSES(&cookie_gateway);

PROCESS_THREAD(cookie_gateway, ev, data)
{
    PROCESS_BEGIN();

    cookie_regout0_fix();

    printf("cookie gateway (Contiki-NG): RPL root + CoAP server\n");

    nrf_gpio_pin_clear(GW_LED_RED);
    nrf_gpio_cfg_output(GW_LED_RED);
    nrf_gpio_pin_set(GW_LED_GREEN);      /* steady green: gateway is up */
    nrf_gpio_cfg_output(GW_LED_GREEN);

    /* Stable global address fd00::1 and DAG prefix fd00::/64, so the sensor
     * node can target a known collector address. */
    uip_ipaddr_t ipaddr;
    uip_ip6addr(&ipaddr, 0xfd00, 0, 0, 0, 0, 0, 0, 1);
    uip_ds6_addr_add(&ipaddr, 0, ADDR_MANUAL);
    NETSTACK_ROUTING.root_set_prefix(&ipaddr, NULL);
    NETSTACK_ROUTING.root_start();

    coap_activate_resource(&res_sensors, "sensors/data");

    PROCESS_END();
}
