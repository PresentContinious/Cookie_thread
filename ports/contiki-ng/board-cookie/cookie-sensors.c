/*
 * Sensors table for the Cookie nRF V2.00 (Contiki-NG). Only the on-die
 * temperature sensor is exported here; the SHTC3 is read directly in the
 * application over bit-banged I2C, not through the Contiki sensors API.
 */
#include "contiki.h"
#include "lib/sensors.h"
#include "common/temperature-sensor.h"

SENSORS(&temperature_sensor);
