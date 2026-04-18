#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "RTClib.h"

// ═══════════════════════════════════════════════════════════
//  HARDWARE CONFIGURATION
// ═══════════════════════════════════════════════════════════

// Display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// I2C Devices
RTC_DS1307 rtc;
Adafruit_MPU6050 mpu;

// Temperature Sensor (OneWire)
#define TEMP_PIN 4
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// Pin Definitions
const int GSR_PIN = 0;        // Galvanic Skin Response (Analog)
const int HRV_PIN = 1;        // Heart Rate Variability (Analog)
const int MOTOR_PIN = 10;     // Haptic Motor Output
const int BUTTON_PIN = 3;     // Manual Workout Override Button

// ═══════════════════════════════════════════════════════════
//  CALIBRATION THRESHOLDS
// ═══════════════════════════════════════════════════════════

const int GSR_STRESS_THRESHOLD = 2500;   // High skin conductivity
const int HRV_STRESS_THRESHOLD = 1500;   // Low heart variability
const float MOTION_THRESHOLD = 15.0;     // Total acceleration (m/s²)

// Hysteresis Margins
const float STRESS_ENTRY_DROP = 1.2;     // Drop needed to start alert
const float STRESS_EXIT_MARGIN = 0.3;    // Recovery needed to stop alert

// ═══════════════════════════════════════════════════════════
//  REAL-WORLD HARDENING PARAMETERS
// ═══════════════════════════════════════════════════════════

// FIX 1: SLEW RATE LIMITING (Sensor Decoupling Detection)
const float MAX_TEMP_CHANGE_PER_SECOND = 2.0;  // °C/s - Physics limit
const float MIN_VALID_TEMP = 20.0;             // Below this = sensor error
const float MAX_VALID_TEMP = 42.0;             // Above this = sensor error

// FIX 2: THERMAL REVERSAL DETECTION (Workout vs Stress)
const float WORKOUT_TEMP_RISE_THRESHOLD = 0.5; // Rising temp = workout

// FIX 3: MULTI-SENSOR VOTING (Environmental vs Internal)
const int MIN_VOTES_FOR_STRESS = 2;            // Need 2/3 sensors to agree

// FIX 4: THERMAL OFFSETTING (Self-Heating Compensation)
float boardHeatOffset = 0.0;                   // Learned offset
const float BOARD_HEAT_LEARNING_RATE = 0.001; // Very slow adaptation
const unsigned long WARMUP_TIME = 600000;      // 10 minutes in milliseconds

// ═══════════════════════════════════════════════════════════
//  STATE VARIABLES
// ═══════════════════════════════════════════════════════════

// Temperature tracking
float baselineTemp = 32.0;
float lastValidTemp = 32.0;
float previousTemp = 32.0;
unsigned long lastTempUpdateTime = 0;
bool tempSensorValid = true;

// Thermal trend analysis
float tempTrendSum = 0.0;      // Cumulative temperature change
int trendSampleCount = 0;
const int TREND_WINDOW = 25;   // 5 seconds at 200ms intervals

// EMA for baseline
const float ALPHA = 0.005;

// Stress detection
bool isStressedInternal = false;
unsigned long stressStartTime = 0;
bool stressTimerActive = false;
const unsigned long STRESS_CONFIRMATION_TIME = 5000;

// Workout state
bool manualWorkoutMode = false;
bool lastButtonState = HIGH;

// System uptime tracking
unsigned long systemStartTime = 0;

// ═══════════════════════════════════════════════════════════
//  FUNCTION DECLARATIONS
// ═══════════════════════════════════════════════════════════

void displayError(const char* msg);
String formatTime(DateTime dt);
void renderUI(DateTime now, int gsr, int hrv, float temp, float tempDrop, 
              float motion, bool stressed, bool analyzing, unsigned long elapsed,
              bool workout, bool manualWorkout, bool sensorError, String errorMsg);
bool validateTemperatureReading(float newTemp);
int countStressVotes(int gsr, int hrv, float tempDrop, bool isTempRising);
void updateThermalTrend(float currentTemp);
bool isThermalReversal();

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n╔════════════════════════════════════╗"));
  Serial.println(F("║  HARDENED STRESS WATCH v3.0        ║"));
  Serial.println(F("║  Real-World Bug Protection Active  ║"));
  Serial.println(F("╚════════════════════════════════════╝\n"));
  
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(MOTOR_PIN, LOW);
  
  analogSetAttenuation(ADC_11db);
  
  Wire.begin(8, 9);
  delay(100);
  
  // Initialize OLED
  Serial.print(F("OLED... "));
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("❌ FAIL"));
    while(1) { digitalWrite(MOTOR_PIN, !digitalRead(MOTOR_PIN)); delay(200); }
  }
  Serial.println(F("✓"));
  
  // Initialize RTC
  Serial.print(F("RTC... "));
  if (!rtc.begin()) {
    Serial.println(F("❌ FAIL"));
    displayError("RTC ERROR");
    while(1);
  }
  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  Serial.println(F("✓"));
  
  // Initialize MPU6050
  Serial.print(F("MPU6050... "));
  if (!mpu.begin()) {
    Serial.println(F("❌ FAIL"));
    displayError("IMU ERROR");
    while(1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println(F("✓"));
  
  // Initialize DS18B20
  Serial.print(F("DS18B20... "));
  tempSensor.begin();
  if (tempSensor.getDeviceCount() == 0) {
    Serial.println(F("⚠ NOT FOUND"));
    tempSensorValid = false;
  } else {
    tempSensor.setResolution(12);
    Serial.println(F("✓"));
  }
  
  // Boot animation
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 15);
  display.println(F("STRESS"));
  display.setCursor(15, 35);
  display.println(F("WATCH"));
  display.display();
  delay(2000);
  
  systemStartTime = millis();
  lastTempUpdateTime = millis();
  
  Serial.println(F("\n════════════════════════════════════"));
  Serial.println(F("  SYSTEM READY - HARDENED MODE"));
  Serial.println(F("════════════════════════════════════\n"));
  Serial.println(F("Real-World Protection Systems:"));
  Serial.println(F("  ✓ Slew Rate Limiting"));
  Serial.println(F("  ✓ Thermal Reversal Detection"));
  Serial.println(F("  ✓ Multi-Sensor Voting"));
  Serial.println(F("  ✓ Self-Heating Compensation"));
  Serial.println();
}

// ═══════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
  DateTime now = rtc.now();
  
  // ─────────────────────────────────────
  // 1. SENSOR ACQUISITION
  // ─────────────────────────────────────
  int gsrValue = analogRead(GSR_PIN);
  int hrvValue = analogRead(HRV_PIN);
  
  tempSensor.requestTemperatures();
  float rawTemp = tempSensor.getTempCByIndex(0);
  
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  float totalMotion = sqrt(
    accel.acceleration.x * accel.acceleration.x +
    accel.acceleration.y * accel.acceleration.y +
    accel.acceleration.z * accel.acceleration.z
  );
  
  // ─────────────────────────────────────
  // 2. FIX #1: SLEW RATE LIMITING
  // ─────────────────────────────────────
  bool sensorError = false;
  String errorMsg = "";
  float currentTemp = lastValidTemp; // Default to last known good value
  
  if (validateTemperatureReading(rawTemp)) {
    unsigned long currentTime = millis();
    unsigned long deltaTime = currentTime - lastTempUpdateTime;
    
    if (deltaTime > 0) {
      float tempChange = abs(rawTemp - lastValidTemp);
      float changeRate = (tempChange / deltaTime) * 1000.0; // °C per second
      
      if (changeRate > MAX_TEMP_CHANGE_PER_SECOND) {
        // PHYSICS VIOLATION - Reject this reading
        sensorError = true;
        errorMsg = "SLEW LIMIT";
        Serial.print(F("⚠ SENSOR DECOUPLED! Change rate: "));
        Serial.print(changeRate, 2);
        Serial.println(F("°C/s (Too fast!)"));
        
        // Freeze baseline until sensor stabilizes
        tempSensorValid = false;
      } else {
        // Valid reading - accept it
        currentTemp = rawTemp;
        lastValidTemp = rawTemp;
        lastTempUpdateTime = currentTime;
        tempSensorValid = true;
      }
    } else {
      currentTemp = rawTemp;
      lastValidTemp = rawTemp;
    }
  } else {
    // Temperature out of valid range
    sensorError = true;
    errorMsg = "TEMP RANGE";
    Serial.print(F("⚠ INVALID TEMP: "));
    Serial.print(rawTemp, 2);
    Serial.println(F("°C (Out of range)"));
    tempSensorValid = false;
  }
  
  // ─────────────────────────────────────
  // 3. FIX #4: THERMAL OFFSETTING (Self-Heating Compensation)
  // ─────────────────────────────────────
  unsigned long uptime = millis() - systemStartTime;
  
  if (uptime > WARMUP_TIME && tempSensorValid) {
    // After 10 minutes, learn the board's self-heating offset
    // Assumes user is calm during initial 10 minutes
    float expectedSkinTemp = 33.0; // Average human skin temp
    float measuredOffset = currentTemp - expectedSkinTemp;
    
    // Slowly adapt the offset
    boardHeatOffset = (boardHeatOffset * (1.0 - BOARD_HEAT_LEARNING_RATE)) + 
                      (measuredOffset * BOARD_HEAT_LEARNING_RATE);
  }
  
  // Apply compensation
  float compensatedTemp = currentTemp - boardHeatOffset;
  
  // ─────────────────────────────────────
  // 4. UPDATE THERMAL TREND
  // ─────────────────────────────────────
  updateThermalTrend(compensatedTemp);
  bool isTempRising = isThermalReversal();
  
  // ─────────────────────────────────────
  // 5. MANUAL WORKOUT BUTTON
  // ─────────────────────────────────────
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) {
    if (reading == LOW) {
      manualWorkoutMode = !manualWorkoutMode;
      Serial.print(F("🔘 Manual Workout: "));
      Serial.println(manualWorkoutMode ? F("ON") : F("OFF"));
      digitalWrite(MOTOR_PIN, HIGH);
      delay(100);
      digitalWrite(MOTOR_PIN, LOW);
    }
    delay(50);
    lastButtonState = reading;
  }
  
  // ─────────────────────────────────────
  // 6. WORKOUT DETECTION
  // ─────────────────────────────────────
  bool autoWorkoutDetected = (totalMotion > MOTION_THRESHOLD);
  bool workoutMode = manualWorkoutMode || autoWorkoutDetected;
  
  // ─────────────────────────────────────
  // 7. SMART BASELINE UPDATE (EMA)
  // ─────────────────────────────────────
  if (!isStressedInternal && !workoutMode && tempSensorValid) {
    baselineTemp = (baselineTemp * (1.0 - ALPHA)) + (compensatedTemp * ALPHA);
  }
  
  float tempDrop = baselineTemp - compensatedTemp;
  
  // ─────────────────────────────────────
  // 8. FIX #2 & #3: MULTI-SENSOR VOTING + THERMAL REVERSAL
  // ─────────────────────────────────────
  int stressVotes = countStressVotes(gsrValue, hrvValue, tempDrop, isTempRising);
  
  // ─────────────────────────────────────
  // 9. LATCHING STRESS DETECTION
  // ─────────────────────────────────────
  if (!isStressedInternal) {
    // ENTRY: Need minimum votes + significant temperature drop
    if (stressVotes >= MIN_VOTES_FOR_STRESS && 
        tempDrop > STRESS_ENTRY_DROP &&
        tempSensorValid &&
        !isTempRising) { // Temperature must be FALLING, not rising
      isStressedInternal = true;
      Serial.println(F("🔴 STRESS LATCH ENGAGED (Multi-sensor consensus)"));
    }
  } else {
    // EXIT: Temperature recovered
    if (tempDrop < STRESS_EXIT_MARGIN || isTempRising) {
      isStressedInternal = false;
      Serial.println(F("🟢 STRESS LATCH RELEASED"));
    }
  }
  
  bool stressConditionMet = isStressedInternal && !workoutMode && !sensorError;
  
  // ─────────────────────────────────────
  // 10. 5-SECOND CONFIRMATION TIMER
  // ─────────────────────────────────────
  if (stressConditionMet) {
    if (!stressTimerActive) {
      stressStartTime = millis();
      stressTimerActive = true;
      Serial.println(F("⏱ Stress timer started"));
    }
  } else {
    if (stressTimerActive) {
      Serial.println(F("✓ Stress cleared before 5s"));
    }
    stressTimerActive = false;
  }
  
  unsigned long elapsed = 0;
  bool stressConfirmed = false;
  
  if (stressTimerActive) {
    elapsed = millis() - stressStartTime;
    if (elapsed >= STRESS_CONFIRMATION_TIME) {
      stressConfirmed = true;
    }
  }
  
  // ─────────────────────────────────────
  // 11. HAPTIC ALERT
  // ─────────────────────────────────────
  digitalWrite(MOTOR_PIN, stressConfirmed ? HIGH : LOW);
  
  // ─────────────────────────────────────
  // 12. SERIAL DEBUG
  // ─────────────────────────────────────
  Serial.print(formatTime(now));
  Serial.print(F(" | GSR:"));
  Serial.print(gsrValue);
  Serial.print(F(" HRV:"));
  Serial.print(hrvValue);
  Serial.print(F(" Raw:"));
  Serial.print(rawTemp, 2);
  Serial.print(F(" Comp:"));
  Serial.print(compensatedTemp, 2);
  Serial.print(F(" Base:"));
  Serial.print(baselineTemp, 2);
  Serial.print(F(" Drop:"));
  Serial.print(tempDrop, 2);
  Serial.print(F(" Motion:"));
  Serial.print(totalMotion, 1);
  Serial.print(F(" Votes:"));
  Serial.print(stressVotes);
  Serial.print(F("/3"));
  Serial.print(isTempRising ? F(" ↑RISING") : F(" ↓FALLING"));
  
  if (sensorError) {
    Serial.print(F(" → ⚠ "));
    Serial.println(errorMsg);
  } else if (stressConfirmed) {
    Serial.println(F(" → 🚨 STRESS!"));
  } else if (stressTimerActive) {
    Serial.print(F(" → ⏳ ("));
    Serial.print(elapsed / 1000);
    Serial.println(F("s)"));
  } else if (workoutMode) {
    Serial.println(manualWorkoutMode ? F(" → 🏋️ MANUAL") : F(" → 🏃 AUTO"));
  } else {
    Serial.println(F(" → ✓ Normal"));
  }
  
  // ─────────────────────────────────────
  // 13. RENDER UI
  // ─────────────────────────────────────
  renderUI(now, gsrValue, hrvValue, compensatedTemp, tempDrop, totalMotion,
           stressConfirmed, stressTimerActive, elapsed, workoutMode, 
           manualWorkoutMode, sensorError, errorMsg);
  
  previousTemp = compensatedTemp;
  delay(200);
}

// ═══════════════════════════════════════════════════════════
//  HARDENING FUNCTIONS
// ═══════════════════════════════════════════════════════════

bool validateTemperatureReading(float temp) {
  // Reject readings outside human physiological range
  if (temp < MIN_VALID_TEMP || temp > MAX_VALID_TEMP) {
    return false;
  }
  // Reject sensor error codes
  if (temp == -127.0 || temp == 85.0) { // Common DS18B20 error values
    return false;
  }
  return true;
}

int countStressVotes(int gsr, int hrv, float tempDrop, bool isTempRising) {
  int votes = 0;
  
  // Vote 1: GSR (Sweat)
  if (gsr > GSR_STRESS_THRESHOLD) {
    votes++;
  }
  
  // Vote 2: HRV (Heart rigidity)
  if (hrv < HRV_STRESS_THRESHOLD) {
    votes++;
  }
  
  // Vote 3: Temperature (Cold skin) - BUT NOT if temperature is rising
  if (tempDrop > STRESS_ENTRY_DROP && !isTempRising) {
    votes++;
  }
  
  return votes;
}

void updateThermalTrend(float currentTemp) {
  // Calculate temperature change from previous reading
  float tempChange = currentTemp - previousTemp;
  
  // Add to rolling sum
  tempTrendSum += tempChange;
  trendSampleCount++;
  
  // Keep only last N samples (sliding window)
  if (trendSampleCount > TREND_WINDOW) {
    // Remove oldest sample's contribution (approximation)
    tempTrendSum *= (float)(TREND_WINDOW - 1) / (float)TREND_WINDOW;
    trendSampleCount = TREND_WINDOW;
  }
}

bool isThermalReversal() {
  // FIX #2: Detect if temperature is RISING (workout) vs FALLING (stress)
  if (trendSampleCount < 5) {
    return false; // Not enough data
  }
  
  float avgTrend = tempTrendSum / (float)trendSampleCount;
  
  // If average trend is positive and significant, temp is rising
  return (avgTrend > WORKOUT_TEMP_RISE_THRESHOLD);
}

// ═══════════════════════════════════════════════════════════
//  UI RENDERING
// ═══════════════════════════════════════════════════════════

void renderUI(DateTime now, int gsr, int hrv, float temp, float tempDrop, 
              float motion, bool stressed, bool analyzing, unsigned long elapsed,
              bool workout, bool manualWorkout, bool sensorError, String errorMsg) {
  
  display.clearDisplay();
  
  // TIME DISPLAY
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 5);
  if (now.hour() < 10) display.print('0');
  display.print(now.hour(), DEC);
  display.print(':');
  if (now.minute() < 10) display.print('0');
  display.print(now.minute(), DEC);
  
  // SEPARATOR
  display.drawLine(0, 32, 128, 32, SSD1306_WHITE);
  
  // STATUS
  if (sensorError) {
    // ⚠ SENSOR ERROR
    display.setTextSize(1);
    display.setCursor(10, 40);
    display.println(F("SENSOR ERROR"));
    display.setCursor(25, 52);
    display.println(errorMsg);
    
  } else if (stressed) {
    // 🚨 STRESS ALERT
    display.setTextSize(2);
    display.setCursor(15, 40);
    display.println(F("STRESS!"));
    
  } else if (analyzing) {
    // ⏳ ANALYZING
    display.setTextSize(1);
    display.setCursor(20, 38);
    display.println(F("Analyzing..."));
    
    int progress = map(elapsed, 0, 5000, 0, 128);
    display.drawRect(0, 50, 128, 8, SSD1306_WHITE);
    display.fillRect(0, 50, progress, 8, SSD1306_WHITE);
    
    display.setCursor(52, 58);
    display.print(elapsed / 1000);
    display.print(F("s"));
    
  } else if (workout) {
    // 🏋️ WORKOUT
    display.setTextSize(1);
    display.setCursor(5, 40);
    display.println(manualWorkout ? F("MANUAL WORKOUT") : F("MOTION DETECTED"));
    display.setCursor(15, 52);
    display.print(F("Motion: "));
    display.print(motion, 1);
    
  } else {
    // ✓ NORMAL
    display.setTextSize(1);
    display.setCursor(20, 38);
    display.println(F("Vitals Normal"));
    
    display.setCursor(0, 50);
    display.print(F("G:"));
    display.print(gsr);
    display.print(F(" H:"));
    display.print(hrv);
    
    display.setCursor(0, 58);
    display.print(F("T:"));
    display.print(temp, 1);
    display.print(F(" D:"));
    display.print(tempDrop, 1);
  }
  
  display.display();
}

// ═══════════════════════════════════════════════════════════
//  HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════

String formatTime(DateTime dt) {
  char buffer[20];
  sprintf(buffer, "%02d:%02d:%02d", dt.hour(), dt.minute(), dt.second());
  return String(buffer);
}

void displayError(const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
}
