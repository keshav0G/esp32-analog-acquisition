
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "signal_generator.h"
#include "adc_acquisition.h"

#define UART_PORT UART_NUM_0
#define RX_BUFFER_SIZE 128

static const char *TAG = "MAIN";

static void print_help(void)
{
    printf("\n");
    printf("============================================\n");
    printf("       ESP32 ANALOG SIGNAL GENERATOR\n");
    printf("============================================\n");

    printf("\nCOMMAND FORMAT:\n");
    printf("  MODE <type> <value>\n");

    printf("\nAVAILABLE MODES:\n");
    printf("  MODE DC <code>       DC signal (0-255)\n");
    printf("  MODE AC <frequency>  Sine wave (1-100 Hz)\n");

    printf("\nEXAMPLES:\n");
    printf("  MODE DC 128\n");
    printf("  MODE AC 10\n");
    printf("  MODE AC 30\n");
    printf("  MODE AC 70\n");

    printf("\nOTHER COMMANDS:\n");
    printf("  STOP                 Stop signal generation\n");
    printf("  HELP                 Display help\n");

    printf("  READ                 Read ADC 10 times\n");
    printf("  CAPTURE <seconds>   Continuous ADC capture (1-10 s)\n");
    printf("============================================\n\n");
    printf("  LOAD ON              Enable CPU stress workload\n");
    printf("  LOAD OFF             Disable CPU stress workload\n");
    printf("  LOAD STATUS          Show CPU load state\n");
}


static void process_command(char *line)
{
    char command[16] = {0};
    char signal_type[16] = {0};
    unsigned int value = 0;
    char extra;

    // Read the command first
    if (sscanf(line, "%15s", command) != 1) {
        return;
    }

    if (strcmp(command, "STOP") == 0) {
        signal_generator_stop();
        printf("Signal generation stopped.\n");
        return;
    }

    if (strcmp(command, "HELP") == 0) {
        print_help();
        return;
    }

    if (strcmp(command, "READ") == 0) {
        printf("\nADC readings from GPIO34:\n");

        for (int i = 0; i < 10; i++) {
            int raw = adc_read_raw();

            if (raw < 0) {
                printf("ADC read failed!\n");
                break;
            }

            printf("Sample %d: %d\n", i + 1, raw);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        printf("\n");
        return;
    }

    if (strcmp(command, "CAPTURE") == 0) {
        unsigned int duration = 0;
        char capture_command[16];

        int capture_count = sscanf(
            line, "%15s %u %c",
            capture_command, &duration, &extra
        );

        if (capture_count != 2 || duration < 1 || duration > 10) {
            printf("Usage: CAPTURE <seconds> (1-10)\n");
            return;
        }

        adc_capture(duration);
        return;
    }

    if (strcmp(command, "LOAD") == 0) {

    char state[16] = {0};

    int count = sscanf(line, "%15s %15s %c",
                       command, state, &extra);

    if (count != 2) {
        printf("Usage: LOAD <ON/OFF/STATUS>\n");
        return;
    }

    if (strcmp(state, "ON") == 0) {
        adc_set_cpu_load(true);
    }
    else if (strcmp(state, "OFF") == 0) {
        adc_set_cpu_load(false);
    }
    else if (strcmp(state, "STATUS") == 0) {
        printf("CPU load: %s\n",
               adc_cpu_load_enabled() ? "ON" : "OFF");
    }
    else {
        printf("Invalid LOAD option. Use ON, OFF or STATUS.\n");
    }

    return;
}

    if (strcmp(command, "MODE") == 0) {
        int count = sscanf(
            line, "%15s %15s %u %c",
            command, signal_type, &value, &extra
        );

        if (count != 3) {
            printf("Usage: MODE <DC/AC> <value>\n");
            return;
        }

        if (strcmp(signal_type, "DC") == 0) {
            if (value > 255) {
                printf("DC code must be between 0 and 255.\n");
                return;
            }

            signal_generator_set_dc((uint8_t)value);
        }
        else if (strcmp(signal_type, "AC") == 0) {
            if (value < 1 || value > 100) {
                printf("Frequency must be between 1 and 100 Hz.\n");
                return;
            }

            signal_generator_start_sine(value);
        }
        else {
            printf("Invalid mode. Choose DC or AC.\n");
        }

        return;
    }

    printf("Invalid command. Type HELP.\n");
}


void app_main(void)
{
    // Configure UART0 for serial communication
    ESP_ERROR_CHECK(
        uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0)
    );

    ESP_ERROR_CHECK(
        uart_param_config(UART_PORT, &(uart_config_t) {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT
        })
    );

    ESP_LOGI(TAG, "Initializing signal generator");

    signal_generator_init();

    ESP_LOGI(TAG, "Initializing ADC");

    adc_acquisition_init();

    ESP_LOGI(TAG, "ADC initialization complete");

    print_help();

    char input[RX_BUFFER_SIZE];
    int position = 0;
    uint8_t received_char;

    printf("> ");

    while (1) {
        int length = uart_read_bytes(
            UART_PORT,
            &received_char,
            1,
            pdMS_TO_TICKS(100)
        );

        if (length > 0) {
            if (received_char == '\r' || received_char == '\n') {
                if (position > 0) {
                    input[position] = '\0';
                    printf("\n");

                    process_command(input);

                    position = 0;
                    printf("> ");
                }
            }
            else if (received_char == 8 || received_char == 127) {
                if (position > 0) {
                    position--;
                }
            }
            else if (position < RX_BUFFER_SIZE - 1) {
                input[position++] = (char)received_char;
                 putchar(received_char);
                fflush(stdout);
            }
        }
    }
}
