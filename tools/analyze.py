
import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.signal import lfilter

FS = 1022.67  # Measured stored sampling rate
DATA = "data"
PLOTS = "plots"
RESULTS = "results"

os.makedirs(PLOTS, exist_ok=True)
os.makedirs(RESULTS, exist_ok=True)

def load(name):
    path = os.path.join(DATA, name)
    df = pd.read_csv(path)
    return df["adc_raw"].to_numpy(dtype=float)

def frequency_fft(x, fs):
    x = np.asarray(x, dtype=float)
    x = x - np.mean(x)
    window = np.hanning(len(x))
    spectrum = np.abs(np.fft.rfft(x * window))
    freqs = np.fft.rfftfreq(len(x), 1/fs)
    peak = np.argmax(spectrum[1:]) + 1
    return freqs[peak]

def save_plot(name):
    plt.tight_layout()
    plt.savefig(os.path.join(PLOTS, name), dpi=160)
    plt.close()

summary = []

# ---------------- DC ANALYSIS ----------------
dc_codes = [48, 112, 176]

dc_data = {}

for code in dc_codes:
    captures = []

    for suffix in ["a", "b"]:
        filename = f"dc_{code}_{suffix}.csv"
        x = load(filename)
        captures.append(x)

        mean = np.mean(x)
        std = np.std(x)
        minimum = np.min(x)
        maximum = np.max(x)

        # Moving average filter, 5 samples
        filtered = lfilter(np.ones(5)/5, 1, x)
        # Exclude startup transient for fair comparison
        raw_noise = np.std(x[4:] - np.mean(x[4:]))
        filtered_noise = np.std(
            filtered[4:] - np.mean(filtered[4:])
        )

        summary.append({
            "test": filename,
            "mean_adc": mean,
            "minimum": minimum,
            "maximum": maximum,
            "peak_to_peak": maximum-minimum,
            "raw_stddev": std,
            "filtered_stddev": np.std(filtered[4:]),
            "noise_reduction_percent":
                (1-filtered_noise/raw_noise)*100 if raw_noise else 0
        })

    dc_data[code] = captures

# DC repeatability and monotonicity plot
means = [np.mean(np.concatenate(dc_data[c])) for c in dc_codes]
plt.figure()
plt.plot(dc_codes, means, "o-", linewidth=2)
plt.xlabel("DAC digital code")
plt.ylabel("Mean ADC code")
plt.title("DC response: monotonicity")
plt.grid()
save_plot("dc_monotonicity.png")

# DC repeatability plot
plt.figure()
for code in dc_codes:
    vals = [np.mean(x) for x in dc_data[code]]
    plt.plot(["Capture A", "Capture B"], vals, "o-", label=f"DAC {code}")
plt.ylabel("Mean ADC code")
plt.title("DC repeatability across two captures")
plt.legend()
plt.grid()
save_plot("dc_repeatability.png")

# Representative DC filtering comparison
x = dc_data[112][0]
filtered = lfilter(np.ones(5)/5, 1, x)
plt.figure()
n = min(500, len(x))
plt.plot(np.arange(n)/FS, x[:n], alpha=0.6, label="Raw")
plt.plot(np.arange(n)/FS, filtered[:n], label="5-point moving average")
plt.xlabel("Time (s)")
plt.ylabel("ADC code")
plt.title("DC noise reduction")
plt.legend()
plt.grid()
save_plot("dc_filtering.png")

# ---------------- AC ANALYSIS ----------------
for hz in [10, 30, 70]:
    filename = f"ac_{hz}hz.csv"
    x = load(filename)
    t = np.arange(len(x))/FS

    measured = frequency_fft(x, FS)
    error = abs(measured-hz)/hz*100
    centered = x - np.mean(x)
    rms = np.sqrt(np.mean(centered**2))
    p2p = np.ptp(x)

    # Five-point causal moving average
    filtered = lfilter(np.ones(5)/5, 1, x)
    valid = slice(4, None)
    filtered_ac = filtered[valid] - np.mean(filtered[valid])
    raw_ac = x[valid] - np.mean(x[valid])

    attenuation = (
        20*np.log10(
            np.sqrt(np.mean(filtered_ac**2)) /
            np.sqrt(np.mean(raw_ac**2))
        )
    )
    clipping = np.any((x <= 41) | (x >= 4054))
    weak_signal = p2p < 205

    print(f"{hz} Hz: clipping={clipping}, weak={weak_signal}")

    summary.append({
        "test": filename,
        "expected_frequency_hz": hz,
        "measured_frequency_hz": measured,
        "frequency_error_percent": error,
        "peak_to_peak": p2p,
        "ac_rms": rms,
        "filter_attenuation_db": attenuation,
        "filter_group_delay_ms": 2000/FS,
        "possible_clipping": clipping,
        "weak_signal": weak_signal
    })

    # Time plot
    n = min(1500, len(x))
    plt.figure()
    plt.plot(t[:n], x[:n])
    plt.xlabel("Time (s)")
    plt.ylabel("ADC code")
    plt.title(f"{hz} Hz signal - time domain")
    plt.grid()
    save_plot(f"ac_{hz}hz_time.png")

    # FFT plot
    spectrum = np.abs(
        np.fft.rfft(centered*np.hanning(len(x)))
    )
    freqs = np.fft.rfftfreq(len(x), 1/FS)

    plt.figure()
    plt.plot(freqs, spectrum)
    plt.xlim(0, 150)
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude")
    plt.title(f"{hz} Hz signal - FFT")
    plt.grid()
    save_plot(f"ac_{hz}hz_fft.png")

# ---------------- ALIASING ----------------
x = load("ac_70hz.csv")
t_original = np.arange(len(x))/FS

# Interpolate onto a 100 SPS grid WITHOUT anti-alias filtering.
t_100 = np.arange(0, t_original[-1], 0.01)
x_100 = np.interp(t_100, t_original, x)

alias = frequency_fft(x_100, 100)

plt.figure()
plt.plot(t_100[:300], x_100[:300], ".-")
plt.xlabel("Time (s)")
plt.ylabel("ADC code")
plt.title(f"70 Hz software downsampled to 100 SPS: {alias:.2f} Hz alias")
plt.grid()
save_plot("alias_70hz_to_100sps.png")

summary.append({
    "test": "70Hz_downsampled_to_100SPS",
    "expected_frequency_hz": 30,
    "measured_frequency_hz": alias,
    "frequency_error_percent": abs(alias-30)/30*100
})

# ---------------- EXPORT ----------------
df = pd.DataFrame(summary)
df.to_csv(os.path.join(RESULTS, "analysis_summary.csv"), index=False)

print("\n========== ANALYSIS SUMMARY ==========")
print(df.to_string(index=False))
print("\nPlots saved in:", PLOTS)
print("Results saved in:", RESULTS)
