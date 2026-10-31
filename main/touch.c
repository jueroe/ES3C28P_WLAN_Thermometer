#include "touch.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_es3c28p.h"
#include "board_i2c.h"

static const char *TAG = "touch";

#define FT6336_REG_TD_STATUS  0x02 /* number of touch points in the low nibble */
#define FT6336_REG_P1_XH      0x03 /* event flag + X high nibble */
#define FT6336_REG_FIRMWARE   0xA6
#define FT6336_REG_CHIP_ID    0xA3

/* The controller reports pixels, not a pressure reading.  A fixed non-zero
 * strength keeps the "pressed" checks in the rest of the program working. */
#define TOUCH_STRENGTH       1000

static i2c_master_dev_handle_t s_touch;
static bool s_available;

static esp_err_t read_registers(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(s_touch, &reg, 1, data, length, 100);
}

static void touch_reset(void)
{
    if (BOARD_TOUCH_PIN_RST == GPIO_NUM_NC) {
        return;
    }

    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    gpio_set_level(BOARD_TOUCH_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
}

/* Map a controller coordinate (0..max-1) onto the screen axis. */
static int scale_axis(uint16_t value, int raw_max, int length)
{
    if (raw_max <= 0 || raw_max == length) {
        return value;
    }
    if (value >= raw_max) {
        value = (uint16_t)(raw_max - 1);
    }
    return (int)((int32_t)value * (length - 1) / (raw_max - 1));
}

esp_err_t touch_init(void)
{
#if !BOARD_TOUCH_ENABLE
    ESP_LOGW(TAG, "touch disabled in board_es3c28p.h");
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    if (BOARD_TOUCH_PIN_IRQ != GPIO_NUM_NC) {
        const gpio_config_t irq_config = {
            .pin_bit_mask = 1ULL << BOARD_TOUCH_PIN_IRQ,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&irq_config));
    }

    /* The controller needs a reset pulse after the rail came up; without it it
     * sometimes stays silent until the next power cycle. */
    touch_reset();

    s_touch = board_i2c_add_device(BOARD_TOUCH_I2C_ADDRESS, BOARD_TOUCH_I2C_FREQ_HZ);
    if (s_touch == NULL) {
        ESP_LOGE(TAG, "FT6336G device handle not created");
        return ESP_FAIL;
    }

    uint8_t points = 0;
    err = read_registers(FT6336_REG_TD_STATUS, &points, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no FT6336G at 0x%02X (%s) - check SDA=%d, SCL=%d and the reset line",
                 BOARD_TOUCH_I2C_ADDRESS, esp_err_to_name(err), BOARD_I2C_PIN_SDA,
                 BOARD_I2C_PIN_SCL);
        s_touch = NULL;
        return err;
    }

    uint8_t firmware = 0;
    uint8_t chip_id = 0;
    read_registers(FT6336_REG_FIRMWARE, &firmware, 1);
    read_registers(FT6336_REG_CHIP_ID, &chip_id, 1);

    s_available = true;
    ESP_LOGI(TAG, "FT6336G ready at 0x%02X, firmware 0x%02X, chip id 0x%02X, %dx%d, swap=%d mirror=%d/%d",
             BOARD_TOUCH_I2C_ADDRESS, firmware, chip_id, BOARD_TOUCH_X_MAX, BOARD_TOUCH_Y_MAX,
             BOARD_TOUCH_SWAP_XY, BOARD_TOUCH_MIRROR_X, BOARD_TOUCH_MIRROR_Y);
    return ESP_OK;
#endif
}

bool touch_is_available(void)
{
    return s_available;
}

esp_err_t touch_read(touch_state_t *state)
{
    memset(state, 0, sizeof(*state));

    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t points = 0;
    esp_err_t err = read_registers(FT6336_REG_TD_STATUS, &points, 1);
    if (err != ESP_OK) {
        return err;
    }
    points &= 0x0F;
    if (points == 0) {
        return ESP_OK; /* nothing pressed */
    }

    uint8_t data[4]; /* P1_XH, P1_XL, P1_YH, P1_YL */
    err = read_registers(FT6336_REG_P1_XH, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    const uint16_t raw_x = (uint16_t)(((data[0] & 0x0F) << 8) | data[1]);
    const uint16_t raw_y = (uint16_t)(((data[2] & 0x0F) << 8) | data[3]);

    int x;
    int y;
    if (BOARD_TOUCH_SWAP_XY) {
        x = scale_axis(raw_y, BOARD_TOUCH_Y_MAX, BOARD_SCREEN_WIDTH);
        y = scale_axis(raw_x, BOARD_TOUCH_X_MAX, BOARD_SCREEN_HEIGHT);
    } else {
        x = scale_axis(raw_x, BOARD_TOUCH_X_MAX, BOARD_SCREEN_WIDTH);
        y = scale_axis(raw_y, BOARD_TOUCH_Y_MAX, BOARD_SCREEN_HEIGHT);
    }

    if (BOARD_TOUCH_MIRROR_X) {
        x = BOARD_SCREEN_WIDTH - 1 - x;
    }
    if (BOARD_TOUCH_MIRROR_Y) {
        y = BOARD_SCREEN_HEIGHT - 1 - y;
    }

    state->raw_x = raw_x;
    state->raw_y = raw_y;
    state->strength = TOUCH_STRENGTH;
    state->x = x;
    state->y = y;
    state->pressed = true;
    return ESP_OK;
}
