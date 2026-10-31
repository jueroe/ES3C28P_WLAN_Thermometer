/*
 * Shared I2C bus of the ES3C28P board.
 *
 * The capacitive touch controller (FT6336G) and the UNIT ENV III sensors
 * (SHT30, QMP6988) are wired to the same two pins, so the bus is created once
 * here and every module adds its own device handle.
 */

#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

/** Create the bus on the first call, afterwards just return ESP_OK. */
esp_err_t board_i2c_init(void);

/** Bus handle for modules that need it, NULL before board_i2c_init(). */
i2c_master_bus_handle_t board_i2c_bus(void);

/** Add one device to the shared bus.  Returns NULL on failure. */
i2c_master_dev_handle_t board_i2c_add_device(uint8_t address, uint32_t scl_speed_hz);
