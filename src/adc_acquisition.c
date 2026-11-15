
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "adc_acquisition.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ADC_CHANNEL_USED       ADC_CHANNEL_6
#define ADC_SAMPLE_RATE        20000
#define ADC_POOL_SIZE          4096
#define ADC_FRAME_SIZE         256
#define MAX_SAMPLES            12000
#define READ_BUFFER_SIZE       512
#define ADC_DECIMATION         16

static const char *TAG = "ADC";

static adc_oneshot_unit_handle_t oneshot_handle = NULL;
static adc_continuous_handle_t continuous_handle = NULL;

static uint16_t samples[MAX_SAMPLES];
static uint32_t sample_count = 0;
static volatile uint32_t overflow_events = 0;
static uint32_t raw_sample_index = 0;
//cpu load variables
static volatile bool cpu_load_enabled = false;
static volatile uint32_t cpu_load_sink = 0x12345678;
static TaskHandle_t cpu_load_task_handle = NULL;


// Create oneshot driver when needed
static void oneshot_start(void)
{
    if (oneshot_handle != NULL) return;

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(&unit_config, &oneshot_handle)
    );

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            oneshot_handle,
            ADC_CHANNEL_USED,
            &channel_config
        )
    );
}

// Destroy oneshot driver before switching modes
static void oneshot_stop(void)
{
    if (oneshot_handle != NULL) {
        ESP_ERROR_CHECK(adc_oneshot_del_unit(oneshot_handle));
        oneshot_handle = NULL;
    }
}

// Continuous ADC overflow callback
static bool IRAM_ATTR adc_overflow_callback(
    adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t *edata,
    void *user_data)
{
    overflow_events++;
    return false;
}

// Create continuous ADC driver when needed
static void continuous_start(void)
{
    if (continuous_handle != NULL) return;

    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = ADC_POOL_SIZE,
        .conv_frame_size = ADC_FRAME_SIZE
    };

    ESP_ERROR_CHECK(
        adc_continuous_new_handle(
            &handle_config, &continuous_handle
        )
    );

    adc_digi_pattern_config_t pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_USED,
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12
    };

    adc_continuous_config_t config = {
        .sample_freq_hz = ADC_SAMPLE_RATE,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1,
        .adc_pattern = &pattern
    };

    ESP_ERROR_CHECK(
        adc_continuous_config(continuous_handle, &config)
    );

    adc_continuous_evt_cbs_t callbacks = {
        .on_pool_ovf = adc_overflow_callback
    };

    ESP_ERROR_CHECK(
        adc_continuous_register_event_callbacks(
            continuous_handle, &callbacks, NULL
        )
    );
}

static void continuous_stop(void)
{
    if (continuous_handle != NULL) {
        ESP_ERROR_CHECK(
            adc_continuous_deinit(continuous_handle)
        );
        continuous_handle = NULL;
    }
}

static void cpu_load_task(void *arg)
{
    uint32_t x = cpu_load_sink;

    while (1) {

        if (!cpu_load_enabled) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Short CPU workload burst
        for (int i = 0; i < 10000; i++) {
            x = x * 1664525U + 1013904223U;
        }

        cpu_load_sink = x;

        // Give other tasks and the idle task CPU time
        vTaskDelay(1);
    }
}

void adc_set_cpu_load(bool enabled)
{
    cpu_load_enabled = enabled;

    printf("CPU load %s\n", enabled ? "ENABLED" : "DISABLED");
}

bool adc_cpu_load_enabled(void)
{
    return cpu_load_enabled;
}

void adc_acquisition_init(void)
{
    if (cpu_load_task_handle == NULL) {

        BaseType_t result = xTaskCreate(
            cpu_load_task,
            "cpu_load",
            3072,
            NULL,
            2,
            &cpu_load_task_handle
        );

        if (result != pdPASS) {
            printf("Failed to create CPU load task!\n");
            cpu_load_task_handle = NULL;
        }
    }

    ESP_LOGI(TAG, "ADC acquisition module ready");
}

int adc_read_raw(void)
{
    // Switch from continuous to oneshot if necessary
    continuous_stop();

    oneshot_start();

    int raw_value = 0;

    esp_err_t result = adc_oneshot_read(
        oneshot_handle,
        ADC_CHANNEL_USED,
        &raw_value
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Oneshot read failed: %s",
                 esp_err_to_name(result));
        return -1;
    }

    return raw_value;
}

void adc_capture(uint32_t duration_seconds)
{
    if (duration_seconds < 1 || duration_seconds > 10) {
        printf("Duration must be between 1 and 10 seconds.\n");
        return;
    }

    // Switch from oneshot to continuous
    oneshot_stop();
    continuous_start();

    sample_count = 0;
    overflow_events = 0;
    raw_sample_index = 0;

    uint8_t read_buffer[READ_BUFFER_SIZE];

    ESP_ERROR_CHECK(
        adc_continuous_flush_pool(continuous_handle)
    );

    ESP_ERROR_CHECK(
        adc_continuous_start(continuous_handle)
    );

    int64_t start_time = esp_timer_get_time();
    int64_t deadline = start_time +
        ((int64_t)duration_seconds * 1000000LL);

    bool read_error = false;

    while (esp_timer_get_time() < deadline &&
           sample_count < MAX_SAMPLES) {

        uint32_t bytes_read = 0;

        esp_err_t result = adc_continuous_read(
            continuous_handle,
            read_buffer,
            sizeof(read_buffer),
            &bytes_read,
            100
        );

        if (result == ESP_ERR_TIMEOUT) continue;

        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Continuous read failed: %s",
                     esp_err_to_name(result));
            read_error = true;
            break;
        }

        for (uint32_t i = 0;
        i + SOC_ADC_DIGI_RESULT_BYTES <= bytes_read;
        i += SOC_ADC_DIGI_RESULT_BYTES) {

            adc_digi_output_data_t *item =
                (adc_digi_output_data_t *)&read_buffer[i];

        if (item->type1.channel == ADC_CHANNEL_USED) {

            // Keep one sample out of every 20
            if ((raw_sample_index % ADC_DECIMATION) == 0) {

                if (sample_count < MAX_SAMPLES) {
                    samples[sample_count++] = item->type1.data;
                }
            }

            raw_sample_index++;
        }
}
    }

    ESP_ERROR_CHECK(
        adc_continuous_stop(continuous_handle)
    );

    int64_t finish_time = esp_timer_get_time();

    double elapsed =
        (finish_time - start_time) / 1000000.0;

    if (sample_count == 0) {
        printf("No ADC samples captured.\n");
        return;
    }

    uint16_t minimum = 4095;
    uint16_t maximum = 0;
    double sum = 0;

    for (uint32_t i = 0; i < sample_count; i++) {
        uint16_t value = samples[i];

        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;

        sum += value;
    }

    double mean = sum / sample_count;
    double variance = 0;

    for (uint32_t i = 0; i < sample_count; i++) {
        double difference = samples[i] - mean;
        variance += difference * difference;
    }

    double stddev = sqrt(variance / sample_count);
    double effective_rate = sample_count / elapsed;

     // Export raw data as CSV through serial
    printf("DATA_BEGIN\n");
    printf("index,adc_raw\n");

    for (uint32_t i = 0; i < sample_count; i++) {
        printf("%lu,%u\n", (unsigned long)i, samples[i]);
    }

    printf("DATA_END\n");

    printf("\n========== ADC CAPTURE ==========\n");
   
    printf("Duration requested : %lu s\n",
           (unsigned long)duration_seconds);
    printf("Elapsed time       : %.4f s\n", elapsed);
    printf("Hardware ADC rate  : %d SPS\n", ADC_SAMPLE_RATE);
    printf("Decimation factor  : %d\n", ADC_DECIMATION);
    printf("Stored target rate : %d SPS\n",
        ADC_SAMPLE_RATE / ADC_DECIMATION);
    printf("Samples captured   : %lu\n",
           (unsigned long)sample_count);
    printf("Effective rate     : %.2f SPS\n", effective_rate);
    printf("Minimum            : %u\n", minimum);
    printf("Maximum            : %u\n", maximum);
    printf("Mean               : %.2f\n", mean);
    printf("Std deviation      : %.2f\n", stddev);
     printf("CPU load during capture: %s\n",
       adc_cpu_load_enabled() ? "ON" : "OFF");
    printf("Application samples stored: %lu\n",
       (unsigned long)sample_count);

printf("ADC pool overflow events  : %lu\n",
       (unsigned long)overflow_events);
    printf("Read error         : %s\n",
           read_error ? "YES" : "NO");
           
    printf("Raw ADC conversions: %lu\n",
       (unsigned long)raw_sample_index);

    printf("Measured hardware rate: %.2f SPS\n",
       raw_sample_index / elapsed);
    printf("=================================\n");

   
}

void adc_acquisition_deinit(void)
{
    oneshot_stop();
    continuous_stop();
}
