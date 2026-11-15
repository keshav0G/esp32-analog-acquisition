
import serial
import time
import csv
import os

PORT = "COM3"
BAUD = 115200

os.makedirs("data", exist_ok=True)

ser = serial.Serial(PORT, BAUD, timeout=2)
time.sleep(2)
ser.reset_input_buffer()

print("Connected to ESP32.")
print("Enter a command, e.g. MODE DC 112 or MODE AC 30")

command = input("Command: ").strip()
ser.write((command + "\n").encode())

time.sleep(0.5)

duration = input("Capture duration in seconds (1-10): ").strip()
ser.write((f"CAPTURE {duration}\n").encode())

capturing = False
rows = []

print("Waiting for capture data...")

while True:
    line = ser.readline().decode(errors="ignore").strip()

    if not line:
        continue

    if line == "DATA_BEGIN":
        capturing = True
        print("Receiving samples...")
        continue

    if line == "DATA_END":
        break

    if capturing:
        if line.startswith("index,"):
            continue

        try:
            index, value = map(int, line.split(","))
            rows.append((index, value))
        except ValueError:
            pass

ser.close()

filename = f"data/capture_{int(time.time())}.csv"

with open(filename, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow(["index", "adc_raw"])
    writer.writerows(rows)

print(f"Saved {len(rows)} samples to {filename}")
