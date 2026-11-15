#ifndef SIGNAL_GENERATOR_H
#define SIGNAL_GENERATOR_H

#include <stdbool.h>
#include <stdint.h>

void signal_generator_init(void);
void signal_generator_set_dc(uint8_t level);
void signal_generator_start_sine(uint32_t frequency_hz);
void signal_generator_stop(void);

#endif