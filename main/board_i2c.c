#include "board_i2c.h"

#include "esp_log.h"

#include "board_es3c28p.h"

static const char *TAG = "board_i2c";

static i2c_master_bus_handle_t s_bus;

esp_err_t board_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_HOST,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* the modules have their own pull-ups, this is a fallback */
    };

    const esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus(SDA=%d, SCL=%d) failed: %s", BOARD_I2C_PIN_SDA,
                 BOARD_I2C_PIN_SCL, esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }

    ESP_LOGI(TAG, "I2C%d ready: SDA=%d SCL=%d (shared by touch and ENV III)", BOARD_I2C_HOST,
             BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}

i2c_master_dev_handle_t board_i2c_add_device(uint8_t address, uint32_t scl_speed_hz)
{
    if (s_bus == NULL) {
        ESP_LOGE(TAG, "add device 0x%02X before the bus exists", address);
        return NULL;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = scl_speed_hz,
    };

    i2c_master_dev_handle_t device = NULL;
    const esp_err_t err = i2c_master_bus_add_device(s_bus, &device_config, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adding device 0x%02X failed: %s", address, esp_err_to_name(err));
        return NULL;
    }
    return device;
}
