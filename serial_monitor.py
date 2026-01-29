#!/usr/bin/env python3
import serial

def monitor_serial(port, baudrate=115200):
    try:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"Monitoring serial port {port} at {baudrate} baud...")
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line)
    except serial.SerialException as e:
        print(f"Error: {e}")
    except KeyboardInterrupt:
        print("\nExiting serial monitor.")

if __name__ == "__main__":
    monitor_serial("/dev/ttyACM0")