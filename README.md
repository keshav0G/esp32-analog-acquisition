# ESP32 Analog Acquisition & Signal Integrity

An ESP32-based analog signal generation and acquisition system using the built-in DAC and ADC. The project explores hardware-paced sampling, buffered acquisition, signal characterization, filtering, frequency estimation, aliasing and stress testing.

## Hardware

- ESP32-WROOM-32 DevKit
- One jumper wire
- USB connection to laptop

### Signal path

`GPIO25 (DAC output) → jumper wire → GPIO34 (ADC1 input)`

No external voltage source is used.

## Features

- Programmable DAC DC levels: 48, 112 and 176
- Sine-wave generation at 10 Hz, 30 Hz and 70 Hz
- ADC1, 12-bit configuration, 12 dB attenuation
- Continuous hardware-paced ADC acquisition
- 10-second captures at over 1,000 stored samples/second
- CSV export and Python-based analysis
- FFT-based frequency estimation
- Moving-average filtering
- Software aliasing demonstration
- Configurable FreeRTOS CPU-load test
- Jumper-removal fault observation

## Sampling Configuration

| Parameter | Value |
|---|---:|
| Requested ADC rate | 20,000 SPS |
| Measured raw rate | ~16,363 SPS |
| Decimation factor | 16 |
| Measured stored rate | ~1,022.7 SPS |
| ADC resolution | 12 bit |
| ADC input | GPIO34 / ADC1 channel 6 |

The DAC uses the ESP32 one-shot DAC driver while ADC acquisition uses the continuous ADC driver. DAC continuous mode is avoided to prevent the relevant I2S DMA resource conflict.

## Example Results

| Input | Estimated frequency | Error |
|---|---:|---:|
| 10 Hz | 9.9958 Hz | 0.042% |
| 30 Hz | 29.9874 Hz | 0.042% |
| 70 Hz | 70.0637 Hz | 0.091% |

A 70 Hz signal downsampled to 100 SPS produced an approximately 30 Hz alias.

The moving-average filter reduced measured DC noise standard deviation by approximately 55%.

## CPU Load Test

A background FreeRTOS task performs a controlled arithmetic workload during acquisition.

| Metric | Load ON | Load OFF |
|---|---:|---:|
| Samples | 10,232 | 10,232 |
| Effective rate | 1,022.65 SPS | 1,022.71 SPS |
| Pool overflow events | 0 | 0 |
| Read errors | No | No |

## Build and Flash

Developed using PlatformIO, ESP-IDF framework, Espressif32 platform.

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

## Serial Commands

```text
HELP
MODE DC 112
MODE AC 30
STOP
READ
CAPTURE 10
LOAD ON
LOAD OFF
LOAD STATUS
```

## Python Analysis

Install dependencies:

```bash
pip install numpy pandas matplotlib scipy pyserial
```

Run:

```bash
python tools/analyze.py
python tools/test_analysis.py
```

## Measurement Limitations

- ADC results are reported as raw, uncalibrated codes; no laboratory-grade voltage accuracy is claimed.
- The floating ADC input during jumper removal is ambiguous and is treated as a low-confidence fault observation.
- The reported overflow counter counts ADC pool overflow events, not exact lost conversions.
- The application sample count is not the ADC driver's internal buffer high-water mark.
- Software downsampling demonstrates aliasing; it is not a new physical measurement.

## Repository Contents

- `src/` — ESP32 firmware
- `include/` — Header files
- `tools/` — Capture, analysis and deterministic test scripts
- `data/` — Raw ADC capture CSV files
- `plots/` — Analysis plots
- `results/` — Measurement summaries
