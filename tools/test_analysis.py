
import numpy as np

FS = 1000.0

def estimate_frequency(x, fs):
    x = x - np.mean(x)
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    freqs = np.fft.rfftfreq(len(x), 1/fs)
    return freqs[np.argmax(spectrum[1:]) + 1]

def moving_average(x, n=5):
    return np.convolve(x, np.ones(n)/n, mode="valid")

# TEST 1: Frequency estimator
for hz in [10, 30, 70]:
    t = np.arange(10000)/FS
    signal = np.sin(2*np.pi*hz*t)
    measured = estimate_frequency(signal, FS)
    error = abs(measured-hz)/hz*100
    assert error < 1.0, f"Frequency test failed: {hz} Hz"
    print(f"PASS: {hz} Hz estimated as {measured:.2f} Hz")

# TEST 2: Moving-average noise reduction
rng = np.random.default_rng(42)
dc = 1500 + rng.normal(0, 20, 10000)
filtered = moving_average(dc, 5)
assert np.std(filtered) < np.std(dc)
print("PASS: Moving average reduces DC noise")

# TEST 3: Aliasing
t = np.arange(10000)/FS
signal = np.sin(2*np.pi*70*t)
downsampled = signal[::10]
alias = estimate_frequency(downsampled, 100)
assert abs(alias-30) < 1.0
print(f"PASS: 70 Hz aliases to {alias:.2f} Hz at 100 SPS")

print("\nAll deterministic tests passed.")
