#include "sensor.h"

#include <stdlib.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board_es3c28p.h"
#include "board_i2c.h"
#include "netcfg.h"

static const char *TAG = "sensor";

/* ------------------------------------------------------------------------- */
/* SHT30 - temperature and humidity                                          */
/* ------------------------------------------------------------------------- */

#define SHT30_CMD_MEASURE_HIGH  0x2400 /* single shot, high repeatability, no clock stretching */
#define SHT30_MEASURE_MS        20

/* ------------------------------------------------------------------------- */
/* QMP6988 - barometric pressure                                             */
/* ------------------------------------------------------------------------- */

#define QMP6988_REG_CHIP_ID     0xD1
#define QMP6988_CHIP_ID         0x5C
#define QMP6988_REG_RESET       0xE0
#define QMP6988_REG_CONFIG      0xF1
#define QMP6988_REG_CTRL_MEAS   0xF4
#define QMP6988_REG_DATA        0xF7 /* pressure MSB .. temperature LSB, 6 bytes */
#define QMP6988_CALIB_START     0xA0
#define QMP6988_CALIB_LENGTH    25

#define QMP6988_CTRL_FORCED     0x01
#define QMP6988_OSR_P_8X        (0x04 << 2)
#define QMP6988_OSR_T_1X        (0x01 << 5)
#define QMP6988_FILTER_4X       0x02
#define QMP6988_MEASURE_MS      30
#define QMP6988_RAW_MIDPOINT    8388608

typedef struct {
    int32_t a0;
    int32_t b00;
    int32_t a1;
    int32_t a2;
    int64_t bt1;
    int64_t bt2;
    int64_t bp1;
    int64_t b11;
    int64_t bp2;
    int64_t b12;
    int64_t b21;
    int64_t bp3;
} qmp6988_calibration_t;

static i2c_master_dev_handle_t s_sht30;
static i2c_master_dev_handle_t s_qmp6988;
static uint8_t s_sht30_address;
static uint8_t s_qmp6988_address;
static qmp6988_calibration_t s_qmp_cal;
static bool s_qmp6988_available;
static bool s_available;

/* Last successful reading, so the HTTP endpoint can answer without touching the
 * I2C bus (the display task reads once every 10 s). */
static sensor_reading_t s_last;
static int64_t s_last_us;
static bool s_last_valid;
static SemaphoreHandle_t s_last_lock;

/* Last pressure that could be read; used when the QMP6988 misses one cycle. */
static float s_last_pressure_hpa;

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static esp_err_t read_registers(i2c_master_dev_handle_t device, uint8_t reg, uint8_t *data,
                                size_t length)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, 100);
}

static esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t buffer[2] = {reg, value};
    return i2c_master_transmit(device, buffer, sizeof(buffer), 100);
}

static bool sht30_crc8(const uint8_t *data, size_t length, uint8_t checksum)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc == checksum;
}

/* ------------------------------------------------------------------------- */
/* SHT30                                                                     */
/* ------------------------------------------------------------------------- */

static esp_err_t sht30_read(float *temperature_c, float *humidity_rh)
{
    const uint8_t command[2] = {SHT30_CMD_MEASURE_HIGH >> 8, SHT30_CMD_MEASURE_HIGH & 0xFF};
    esp_err_t err = i2c_master_transmit(s_sht30, command, sizeof(command), 100);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(SHT30_MEASURE_MS));

    uint8_t data[6];
    err = i2c_master_receive(s_sht30, data, sizeof(data), 100);
    if (err != ESP_OK) {
        return err;
    }

    if (!sht30_crc8(&data[0], 2, data[2]) || !sht30_crc8(&data[3], 2, data[5])) {
        ESP_LOGW(TAG, "SHT30 CRC mismatch (raw %02X %02X %02X %02X %02X %02X)", data[0], data[1],
                 data[2], data[3], data[4], data[5]);
        return ESP_ERR_INVALID_CRC;
    }

    const uint32_t raw_temperature = ((uint32_t)data[0] << 8) | data[1];
    const uint32_t raw_humidity = ((uint32_t)data[3] << 8) | data[4];

    float humidity = 100.0f * (float)raw_humidity / 65535.0f;
    if (humidity < 0.0f) {
        humidity = 0.0f;
    }
    if (humidity > 100.0f) {
        humidity = 100.0f;
    }

    *temperature_c = -45.0f + 175.0f * (float)raw_temperature / 65535.0f;
    *humidity_rh = humidity;
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* QMP6988                                                                   */
/* ------------------------------------------------------------------------- */

static int16_t be16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

/*
 * Compensation from the QMP6988 datasheet, in the same integer form used by the
 * M5Stack ENV III driver and esp-idf-lib.  The intermediate results need 64 bit.
 */
static int16_t qmp6988_temperature_q8(const qmp6988_calibration_t *cal, int32_t raw)
{
    const int64_t dt = raw;
    int64_t wk1 = cal->a1 * dt;         /* 54Q23 */
    int64_t wk2 = (cal->a2 * dt) >> 14; /* 39Q33 */
    wk2 = (wk2 * dt) >> 10;             /* 52Q23 */
    wk2 = ((wk1 + wk2) / 32767) >> 19;  /* 20Q4 */
    return (int16_t)((cal->a0 + wk2) >> 4);
}

static int32_t qmp6988_pressure_q4(const qmp6988_calibration_t *cal, int32_t raw, int16_t temperature)
{
    const int64_t dp = raw;
    const int64_t tx = temperature;
    int64_t wk1 = cal->bt1 * tx;
    int64_t wk2 = (cal->bp1 * dp) >> 5;
    wk1 += wk2;

    wk2 = (cal->bt2 * tx) >> 1;
    wk2 = (wk2 * tx) >> 8;
    int64_t wk3 = wk2;

    wk2 = (cal->b11 * tx) >> 4;
    wk2 = (wk2 * dp) >> 1;
    wk3 += wk2;

    wk2 = (cal->bp2 * dp) >> 13;
    wk2 = (wk2 * dp) >> 1;
    wk3 += wk2;

    wk1 += wk3 >> 14;

    wk2 = cal->b12 * tx;
    wk2 = (wk2 * tx) >> 22;
    wk2 = (wk2 * dp) >> 1;
    wk3 = wk2;

    wk2 = (cal->b21 * tx) >> 6;
    wk2 = (wk2 * dp) >> 23;
    wk2 = (wk2 * dp) >> 1;
    wk3 += wk2;

    wk2 = (cal->bp3 * dp) >> 12;
    wk2 = (wk2 * dp) >> 23;
    wk2 = wk2 * dp;
    wk3 += wk2;

    wk1 += wk3 >> 15;
    wk1 /= 32767;
    wk1 >>= 11;      /* Q15 -> Q4 */
    wk1 += cal->b00; /* 20Q4 */

    return (int32_t)wk1;
}

static esp_err_t qmp6988_read_calibration(void)
{
    uint8_t data[QMP6988_CALIB_LENGTH];
    esp_err_t err = read_registers(s_qmp6988, QMP6988_CALIB_START, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    s_qmp_cal.a0 = ((int32_t)data[18] << 12) | ((int32_t)data[19] << 4) | (data[24] & 0x0F);
    s_qmp_cal.b00 = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((data[24] & 0xF0) >> 4);

    const int32_t a1 = be16(&data[20]);
    const int32_t a2 = be16(&data[22]);
    const int64_t bt1 = be16(&data[2]);
    const int64_t bt2 = be16(&data[4]);
    const int64_t bp1 = be16(&data[6]);
    const int64_t b11 = be16(&data[8]);
    const int64_t bp2 = be16(&data[10]);
    const int64_t b12 = be16(&data[12]);
    const int64_t b21 = be16(&data[14]);
    const int64_t bp3 = be16(&data[16]);

    s_qmp_cal.a1 = (int32_t)(3608LL * a1 - 1731677965LL);
    s_qmp_cal.a2 = (int32_t)(16889LL * a2 - 87619360LL);
    s_qmp_cal.bt1 = 2982LL * bt1 + 107370906LL;
    s_qmp_cal.bt2 = 329854LL * bt2 + 108083093LL;
    s_qmp_cal.bp1 = 19923LL * bp1 + 1133836764LL;
    s_qmp_cal.b11 = 2406LL * b11 + 118215883LL;
    s_qmp_cal.bp2 = 3079LL * bp2 - 181579595LL;
    s_qmp_cal.b12 = 6846LL * b12 + 85590281LL;
    s_qmp_cal.b21 = 13836LL * b21 + 79333336LL;
    s_qmp_cal.bp3 = 2915LL * bp3 + 157155561LL;
    return ESP_OK;
}

static esp_err_t qmp6988_init(void)
{
    uint8_t chip_id = 0;
    esp_err_t err = read_registers(s_qmp6988, QMP6988_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK || chip_id != QMP6988_CHIP_ID) {
        ESP_LOGE(TAG, "no QMP6988 at 0x%02X (chip id 0x%02X, expected 0x%02X, %s)",
                 s_qmp6988_address, chip_id, QMP6988_CHIP_ID, esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    err = write_register(s_qmp6988, QMP6988_REG_RESET, 0xE6);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        err = qmp6988_read_calibration();
    }
    if (err == ESP_OK) {
        err = write_register(s_qmp6988, QMP6988_REG_CONFIG, QMP6988_FILTER_4X);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "QMP6988 setup failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "QMP6988 ready at 0x%02X (chip id 0x%02X)", s_qmp6988_address, chip_id);
    return ESP_OK;
}

static esp_err_t qmp6988_read(float *pressure_hpa)
{
    const uint8_t control = QMP6988_CTRL_FORCED | QMP6988_OSR_P_8X | QMP6988_OSR_T_1X;
    esp_err_t err = write_register(s_qmp6988, QMP6988_REG_CTRL_MEAS, control);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(QMP6988_MEASURE_MS));

    uint8_t data[6];
    err = read_registers(s_qmp6988, QMP6988_REG_DATA, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    const int32_t raw_pressure =
        (((int32_t)data[0] << 16) | ((int32_t)data[1] << 8) | data[2]) - QMP6988_RAW_MIDPOINT;
    const int32_t raw_temperature =
        (((int32_t)data[3] << 16) | ((int32_t)data[4] << 8) | data[5]) - QMP6988_RAW_MIDPOINT;

    const int16_t temperature = qmp6988_temperature_q8(&s_qmp_cal, raw_temperature);
    const int32_t pressure_q4 = qmp6988_pressure_q4(&s_qmp_cal, raw_pressure, temperature);

    /* Q4 in Pa */
    const float hpa = (float)pressure_q4 / 16.0f / 100.0f;
    if (hpa < 250.0f || hpa > 1200.0f) {
        ESP_LOGW(TAG, "QMP6988 pressure out of range (raw %ld -> %d hPa*16)", (long)raw_pressure,
                 (int)pressure_q4);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *pressure_hpa = hpa;
    return ESP_OK;
}

/* Log every address that answers, so a wrong jumper or a swapped cable is
 * visible in the serial log instead of only as "sensor not found". */
static void scan_bus(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    if (bus == NULL) {
        return;
    }

    for (uint8_t address = 0x08; address < 0x78; address++) {
        if (i2c_master_probe(bus, address, 20) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
        }
    }
}

/* The ENV III uses 0x44/0x70; the alternative addresses cover units with the
 * address pins tied the other way (SHT30 0x45, QMP6988 0x56). */
static uint8_t find_address(const uint8_t *candidates, size_t count)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();
    for (size_t i = 0; i < count; i++) {
        if (i2c_master_probe(bus, candidates[i], 20) == ESP_OK) {
            return candidates[i];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

esp_err_t sensor_init(void)
{
    if (s_last_lock == NULL) {
        s_last_lock = xSemaphoreCreateMutex();
        if (s_last_lock == NULL) {
            ESP_LOGE(TAG, "no memory for the reading lock");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = board_i2c_init();
    if (err != ESP_OK) {
        return err;
    }

    scan_bus();

    static const uint8_t sht30_addresses[] = {BOARD_SHT30_ADDRESS, 0x45};
    static const uint8_t qmp6988_addresses[] = {BOARD_QMP6988_ADDRESS, 0x56};

    s_sht30_address = find_address(sht30_addresses, sizeof(sht30_addresses));
    s_qmp6988_address = find_address(qmp6988_addresses, sizeof(qmp6988_addresses));

    if (s_sht30_address == 0) {
        ESP_LOGE(TAG, "no SHT30 found at 0x%02X/0x45 - check SDA=%d, SCL=%d, 3V3 and the ENV III cable",
                 BOARD_SHT30_ADDRESS, BOARD_I2C_PIN_SDA, BOARD_I2C_PIN_SCL);
        return ESP_ERR_NOT_FOUND;
    }

    s_sht30 = board_i2c_add_device(s_sht30_address, BOARD_I2C_FREQ_HZ);
    if (s_qmp6988_address != 0) {
        s_qmp6988 = board_i2c_add_device(s_qmp6988_address, BOARD_I2C_FREQ_HZ);
    }

    if (s_sht30 == NULL) {
        ESP_LOGE(TAG, "SHT30 device handle not created");
        return ESP_FAIL;
    }

    float temperature = 0.0f;
    float humidity = 0.0f;
    /* After power-up the SHT30 occasionally ignores the first command; a short
     * retry is cheaper than a comment "works after reset". */
    for (int attempt = 0; attempt < 5; attempt++) {
        err = sht30_read(&temperature, &humidity);
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHT30 at 0x%02X answers but delivers no measurement (%s)", s_sht30_address,
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SHT30 ready at 0x%02X, first reading %d.%d C, %d.%d %%rH", s_sht30_address,
             (int)temperature, abs((int)(temperature * 10) % 10), (int)humidity,
             abs((int)(humidity * 10) % 10));

    if (s_qmp6988 != NULL && qmp6988_init() == ESP_OK) {
        s_qmp6988_available = true;
    } else {
        ESP_LOGW(TAG, "QMP6988 not available, the pressure stays at the last value");
    }

    s_available = true;

    /* Wait until a complete measurement can be read, so the caller always gets
     * valid data. */
    sensor_reading_t probe;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (sensor_read(&probe) == ESP_OK) {
            ESP_LOGI(TAG, "sensor ready: %d.%d C, %d.%d %%rH, %d hPa",
                     (int)probe.temperature_c, abs((int)(probe.temperature_c * 10) % 10),
                     (int)probe.humidity_rh, abs((int)(probe.humidity_rh * 10) % 10),
                     (int)(probe.pressure_hpa + 0.5f));
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGE(TAG, "SHT30 answers but delivers no measurement");
    s_available = false;
    return ESP_FAIL;
}

bool sensor_is_available(void)
{
    return s_available;
}

esp_err_t sensor_read(sensor_reading_t *reading)
{
    if (!s_available) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(reading, 0, sizeof(*reading));

    esp_err_t err = sht30_read(&reading->temperature_c, &reading->humidity_rh);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHT30 read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (s_qmp6988_available && qmp6988_read(&reading->pressure_hpa) == ESP_OK) {
        s_last_pressure_hpa = reading->pressure_hpa;
    } else {
        /* keep the last pressure instead of showing 0 hPa */
        reading->pressure_hpa = s_last_pressure_hpa;
    }

    /* calibration: the configured offset applies to every reading, so the
     * display and the JSON endpoint show the same corrected value */
    const int offset = netcfg_temp_offset_tenths();
    if (offset != 0) {
        reading->temperature_c += (float)offset / 10.0f;
    }

    if (s_last_lock != NULL) {
        xSemaphoreTake(s_last_lock, portMAX_DELAY);
        s_last = *reading;
        s_last_us = esp_timer_get_time();
        s_last_valid = true;
        xSemaphoreGive(s_last_lock);
    }
    return ESP_OK;
}

bool sensor_last(sensor_reading_t *reading, int64_t *age_ms)
{
    if (s_last_lock == NULL) {
        return false;
    }

    xSemaphoreTake(s_last_lock, portMAX_DELAY);
    const bool valid = s_last_valid;
    if (valid) {
        if (reading != NULL) {
            *reading = s_last;
        }
        if (age_ms != NULL) {
            *age_ms = (esp_timer_get_time() - s_last_us) / 1000;
        }
    }
    xSemaphoreGive(s_last_lock);
    return valid;
}
