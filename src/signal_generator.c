
#include <stdint.h>

#include "signal_generator.h"
#include "driver/dac_oneshot.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

#define TABLE_SIZE 32

static const char *TAG = "SIGNAL_GEN";

static dac_oneshot_handle_t dac_handle;
static esp_timer_handle_t waveform_timer;

static uint32_t sample_index = 0;

// 32 samples representing one sine-wave cycle
static const uint8_t sine_table[TABLE_SIZE] = {
    128, 148, 166, 182, 198, 209, 219, 225,
    228, 225, 219, 209, 198, 182, 166, 148,
    128, 108,  90,  74,  58,  47,  37,  31,
     28,  31,  37,  47,  58,  74,  90, 108
};

// Called periodically to update the DAC
static void waveform_callback(void *arg)
{
    uint8_t value = sine_table[sample_index];

    dac_oneshot_output_voltage(dac_handle, value);

    sample_index++;

    if (sample_index >= TABLE_SIZE) {
        sample_index = 0;
    }
}

void signal_generator_init(void)
{
    // Initialize DAC channel 0 (GPIO25)
    dac_oneshot_config_t dac_config = {
        .chan_id = DAC_CHAN_0
    };

    ESP_ERROR_CHECK(
        dac_oneshot_new_channel(&dac_config, &dac_handle)
    );

    // Create timer for waveform generation
    esp_timer_create_args_t timer_args = {
        .callback = waveform_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "waveform_timer"
    };

    ESP_ERROR_CHECK(
        esp_timer_create(&timer_args, &waveform_timer)
    );

    ESP_LOGI(TAG, "DAC initialized on GPIO25");
}

void signal_generator_stop(void)
{
    // Stop timer if it is running
    esp_err_t err = esp_timer_stop(waveform_timer);

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    sample_index = 0;
}

void signal_generator_set_dc(uint8_t level)
{
    signal_generator_stop();

    ESP_ERROR_CHECK(
        dac_oneshot_output_voltage(dac_handle, level)
    );

    ESP_LOGI(TAG, "DC output set to DAC code %u", level);
}

void signal_generator_start_sine(uint32_t frequency_hz)
{
    signal_generator_stop();

    sample_index = 0;

    // DAC update rate = waveform frequency * table size
    uint64_t interval_us =
        1000000ULL / ((uint64_t)frequency_hz * TABLE_SIZE);

    ESP_LOGI(TAG, "Starting sine wave: %lu Hz",
             (unsigned long)frequency_hz);

    ESP_LOGI(TAG, "DAC update interval: %llu us",
             (unsigned long long)interval_us);

    ESP_ERROR_CHECK(
        esp_timer_start_periodic(waveform_timer, interval_us)
    );
}
