# ⌚ Advanced Biometric Stress Watch v3.0

> A state-aware, real-time biometric fusion engine built on the ESP32-C3. 

This project is a custom wearable physiological stress monitor designed to track autonomic nervous system activity. By combining peripheral temperature, Galvanic Skin Response (GSR), and Heart Rate Variability (HRV) with advanced mathematical heuristics, the firmware accurately distinguishes between physical exertion and psychological stress.

## 🛠️ Hardware Stack
* **Microcontroller:** ESP32-C3 (Low-power, Wi-Fi enabled for future OTA updates)
* **Sensors:**
    * DS18B20 Temperature Sensor (Configured for 12-bit precision: 0.0625°C resolution)
    * MPU6050 6-DoF IMU (Motion artifact filtering)
    * Analog GSR & HRV Sensors
* **Peripherals:** I2C OLED Display, RTC Module, Physical Override Button

## 🧠 Core Software Architecture & Features

### 1. Dynamic Baseline Filtering (EMA)
Instead of static boot-time calibration, the system utilizes an **Exponential Moving Average (EMA)** to adapt to natural environmental changes. The EMA update intentionally freezes during a detected stress event to prevent the baseline from artificially shifting during an episode.

### 2. Latching Hysteresis Logic
Human peripheral temperature recovers slowly. To prevent the "Snapshot Flaw" (where a slight warming immediately clears a stress alert), the firmware implements dual-threshold latching:
* **Entry Threshold:** Triggers upon a sudden, sustained temperature drop.
* **Exit Margin:** Latches the alert state until temperature fully recovers to near-baseline, modeling actual biological recovery time.

### 3. Multi-Sensor Voting & Slew Rate Limiting
* **Thermal Reversal:** Cross-references rising temperature trends with high GSR and IMU activity to categorize states as "Active Workout" rather than "Panic Attack."
* **Decoupling Detection:** Rejects biometric data if the temperature change exceeds 2.0°C per second (a biologically impossible slew rate indicative of a loose wearable strap).

## 🚀 Development & Debugging Insights
Building this prototype required bridging the gap between theoretical software and physical hardware realities:
* **Environment Optimization:** Navigated local VS Code and PlatformIO toolchain instability via deep cache resets and Wokwi simulator integration.
* **Hardware Interfacing:** Resolved "OneWire Ghost" errors (-127°C dead zones), I2C boot traps, and implemented internal pull-up resistor (INPUT_PULLUP) debouncing for floating GPIO states.

## 💻 Simulation & Testing (Wokwi Web)
Due to local toolchain instability, the v3.0 architecture was fully built and vetted using the cloud-based Wokwi Simulator. 

**To run the simulation:**
1. Clone this repository or copy the `main.cpp` and `diagram.json` files.
2. Open a new ESP32-C3 project on [Wokwi](https://wokwi.com/).
3. Paste the contents of `diagram.json` into the Wokwi diagram editor to instantly map the exact hardware wiring.
4. Replace the default code with `main.cpp`.

**Required Libraries (Add via Wokwi Library Manager):**
* `OneWire`
* `DallasTemperature`
* `Adafruit MPU6050`
* `Adafruit SSD1306` (or whatever library you used for the OLED)

## 🔮 Future Roadmap
* Refine physical chassis and custom PCB design.
* Implement web-based dashboard integration for long-term data analytics.

## 🧠 Development Approach: Learning with AI
I am currently a first-year engineering student. Writing 500+ lines of complex, state-aware C++ for an ESP32 is currently beyond my raw syntax memorization. Rather than letting that limit the scope of what I could build, I used Gemini and Claude as 24/7 senior engineering tutors.

While I designed the hardware schematic, wired the physical ESP32 components, and solved the local IDE/PlatformIO environment crashes, I collaborated heavily with AI to:
* Translate my biological theories (e.g., how human temperature recovers) into functional C++ mathematical filters.
* Troubleshoot complex I2C hardware boot traps and debounce states.
* Refine my prototype logic into a cleanly structured sensor fusion engine.

This project was an exercise in system architecture and AI-assisted engineering—proving that with the right logic and hardware fundamentals, the gap between an idea and a working embedded prototype is smaller than ever.
