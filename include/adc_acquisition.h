#include <stdbool.h>

#ifndef ADC_ACQUISITION_H
#define ADC_ACQUISITION_H

void adc_acquisition_init(void);
int adc_read_raw(void);
void adc_acquisition_deinit(void);
void adc_capture(uint32_t duration_seconds);
void adc_set_cpu_load(bool enabled);
bool adc_cpu_load_enabled(void);

#endif
