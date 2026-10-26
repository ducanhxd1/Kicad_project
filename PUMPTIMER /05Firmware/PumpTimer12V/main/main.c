#include <stdbool.h>
#include <time.h>

#include "app_nvs.h"
#include "app_spi.h"
#include "board_guard.h"
#include "esp_err.h"
#include "esp_log.h"
#include "fault_history.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_update.h"
#include "pump_controller.h"
#include "pump_display.h"
#include "pump_output.h"
#include "pump_rtc.h"
#include "pump_schedule.h"
#include "pump_stats.h"
#include "pump_storage.h"
#include "runtime_diag.h"
#include "sdkconfig.h"
#include "st7735.h"
#include "time_manager.h"
#include "time_service.h"
#include "web_dashboard.h"
#include "i2c.h"

static const char *TAG = "pump_timer";
static pump_rtc_t s_rtc;
static app_spi_bus_t s_spi_bus;
static st7735_t s_display;

static bool s_display_init(void)
{
    esp_err_t err = app_spi_bus_init(&s_spi_bus, SPI2_HOST,
                                     CONFIG_PUMP_TFT_MOSI_GPIO, GPIO_NUM_NC,
                                     CONFIG_PUMP_TFT_SCLK_GPIO, 1024U);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }
    const st7735_config_t config = {
        .bus = &s_spi_bus,
        .cs_gpio = CONFIG_PUMP_TFT_CS_GPIO,
        .dc_gpio = CONFIG_PUMP_TFT_DC_GPIO,
        .reset_gpio = CONFIG_PUMP_TFT_RST_GPIO,
        .backlight_gpio = GPIO_NUM_NC,
        /* Physical panel 80x160, landscape coordinates 160x80. */
        .width = 160U,
        .height = 80U,
        .x_offset = 0U,
        .y_offset = 24U,
        .clock_hz = 10U * 1000U * 1000U,
        .spi_mode = 0U,
    };
    err = st7735_init(&s_display, &config);
    if (err == ESP_OK) err = st7735_fill_screen(&s_display, ST7735_BLACK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ST7735 init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool s_rtc_bus_init(void)
{
    const i2c_bus_config_t config = {
        .port = I2C_NUM_0,
        .sda_io_num = CONFIG_PUMP_RTC_SDA_GPIO,
        .scl_io_num = CONFIG_PUMP_RTC_SCL_GPIO,
        .clk_speed_hz = 100000U,
    };
    esp_err_t err = i2c_dev_init_config(&config);
    if (err == ESP_OK) err = pump_rtc_init(&s_rtc, I2C_NUM_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 init failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "DS3231 ready: SDA=%d, SCL=%d",
             CONFIG_PUMP_RTC_SDA_GPIO, CONFIG_PUMP_RTC_SCL_GPIO);
    return true;
}

static esp_err_t load_schedule(pump_schedule_config_t *config, bool nvs_ready)
{
    pump_schedule_make_safe(config);
    if (nvs_ready) {
        esp_err_t err = pump_storage_load_or_create(config);
        if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) return ESP_OK;
        ESP_LOGE(TAG, "Schedule storage error; AUTO stays disabled: %s",
                 esp_err_to_name(err));
    }
    pump_schedule_make_safe(config);
    return ESP_OK;
}

void app_main(void)
{
    runtime_diag_log_startup();
    ESP_ERROR_CHECK(fault_history_init());

    esp_err_t nvs_err = app_nvs_init();
    if (nvs_err != ESP_OK) {
        fault_history_record(FAULT_SOURCE_STORAGE, (int32_t)nvs_err);
        ESP_LOGE(TAG, "NVS unavailable; automatic schedule will be disabled: %s",
                 esp_err_to_name(nvs_err));
    } else {
        esp_err_t journal_err = fault_history_enable_persistence();
        if (journal_err != ESP_OK) {
            fault_history_record(FAULT_SOURCE_STORAGE, (int32_t)journal_err);
            ESP_LOGE(TAG, "Persistent fault history unavailable: %s",
                     esp_err_to_name(journal_err));
        }
    }

    esp_err_t stats_err = pump_stats_init(nvs_err == ESP_OK);
    if (stats_err != ESP_OK) {
        fault_history_record(FAULT_SOURCE_STORAGE, (int32_t)stats_err);
        ESP_LOGE(TAG, "Pump statistics unavailable: %s", esp_err_to_name(stats_err));
    }

    esp_reset_reason_t reset_reason = runtime_diag_reset_reason();
    if (runtime_diag_reset_is_abnormal(reset_reason))
        fault_history_record(FAULT_SOURCE_BOOT, (int32_t)reset_reason);

    esp_err_t board_err = board_guard_validate();
    if (board_err != ESP_OK)
        fault_history_record(FAULT_SOURCE_CONFIG, (int32_t)board_err);

    bool display_ready = s_display_init();
    bool rtc_ready = s_rtc_bus_init();

    pump_schedule_config_t schedule_config;
    (void)load_schedule(&schedule_config, nvs_err == ESP_OK);
    esp_err_t schedule_err = pump_schedule_init(&schedule_config);
    bool schedule_ready = schedule_err == ESP_OK;
    if (!schedule_ready) {
        fault_history_record(FAULT_SOURCE_CONFIG, (int32_t)schedule_err);
        ESP_LOGE(TAG, "Schedule engine init failed: %s", esp_err_to_name(schedule_err));
    }

    /* Output initialization puts relays OFF before Wi-Fi/NTP can block. */
    esp_err_t output_err = schedule_ready && board_err == ESP_OK
                               ? pump_output_init() : ESP_ERR_INVALID_STATE;
    bool output_ready = output_err == ESP_OK;
    if (!output_ready && output_err != ESP_ERR_NOT_SUPPORTED) {
        fault_history_record(FAULT_SOURCE_CONFIG, (int32_t)output_err);
        ESP_LOGE(TAG, "Pump output unavailable; relays remain OFF: %s",
                 esp_err_to_name(output_err));
    }

    bool time_service_ready =
        time_service_init(rtc_ready ? &s_rtc : NULL, rtc_ready) == ESP_OK;
    if (time_service_ready) (void)time_service_request_sync();
    ESP_ERROR_CHECK(time_manager_init(rtc_ready ? &s_rtc : NULL, rtc_ready));

    bool controller_ready = false;
    if (output_ready) {
        esp_err_t controller_err = pump_controller_start();
        if (controller_err != ESP_OK) {
            (void)pump_output_stop_all();
            fault_history_record(FAULT_SOURCE_CONTROLLER, (int32_t)controller_err);
            ESP_LOGE(TAG, "Pump controller start failed: %s",
                     esp_err_to_name(controller_err));
        } else controller_ready = true;
    }

    if (time_service_ready) {
        esp_err_t web_err = web_dashboard_start();
        if (web_err != ESP_OK && web_err != ESP_ERR_NOT_SUPPORTED)
            ESP_LOGE(TAG, "Web dashboard start failed: %s", esp_err_to_name(web_err));
    }

    if (display_ready) {
        esp_err_t display_err = pump_display_start(&s_display);
        if (display_err != ESP_OK)
            ESP_LOGE(TAG, "Status display task failed: %s", esp_err_to_name(display_err));
    }
    runtime_diag_log_current_task("app_main_done");

    bool ota_core_ready = nvs_err == ESP_OK && stats_err == ESP_OK &&
                          board_err == ESP_OK && schedule_ready &&
                          output_ready && controller_ready;
    esp_err_t validation_err = ota_update_start_boot_validation(ota_core_ready);
    if (validation_err != ESP_OK && validation_err != ESP_ERR_NOT_SUPPORTED)
        ESP_LOGE(TAG, "OTA boot validation start failed: %s",
                 esp_err_to_name(validation_err));
}
