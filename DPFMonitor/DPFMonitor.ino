#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SplashScreen.h"
#include "BluetoothManager.h"
#include "ELM327Manager.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
#define BUZZER_ENABLED true
#define BUZZER_PIN 33
#define BUZZER_FREQUENCY 2600
#define BUZZER_RESOLUTION 8
#define BUZZER_VOLUME 20

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SplashScreen splash;
BluetoothManager bluetooth;
ELM327Manager elm327;
bool bluetoothStarted = false, elm327Started = false, normalOperationStarted = false, bluetoothConnectedSoundPlayed = false;
bool regenerationActive = false, regenerationCandidate = false;
unsigned long regenerationCandidateStart = 0, regenerationEndStart = 0;
const float REGEN_START_TEMPERATURE = 500.0f, REGEN_END_TEMPERATURE = 450.0f;
const unsigned long REGEN_START_TIME = 60000, REGEN_END_TIME = 60000;

enum DistanceTrend { TREND_UNKNOWN, TREND_UP, TREND_DOWN };
DistanceTrend distanceTrend = TREND_UNKNOWN, pendingTrend = TREND_UNKNOWN;
float previousDistanceKm = NAN;
uint32_t processedDistanceSampleId = 0;
uint8_t matchingTrendSamples = 0;
const float DISTANCE_NOISE_TOLERANCE_KM = 0.05f;
const uint8_t TREND_CONFIRMATION_SAMPLES = 3;

void buzzerOn() {
#if BUZZER_ENABLED
  ledcWrite(BUZZER_PIN, BUZZER_VOLUME);
#endif
}
void buzzerOff() {
#if BUZZER_ENABLED
  ledcWrite(BUZZER_PIN, 0);
#endif
}
void buzzerSignal(unsigned long duration) {
#if BUZZER_ENABLED
  buzzerOn(); delay(duration); buzzerOff();
#endif
}
void playBluetoothConnectedSound() { buzzerSignal(500); delay(250); buzzerSignal(500); }
void playRegenerationStartSound() { buzzerSignal(1000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(2000); }
void playRegenerationEndSound() { buzzerSignal(2000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(1000); delay(250); buzzerSignal(1000); }

void drawBluetoothStatus() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setCursor(0, 0); display.println("DPFMonitor"); display.setCursor(0, 18); display.println("Bluetooth:"); display.setCursor(0, 30);
  switch (bluetooth.getState()) { case BT_STARTING: display.println("Starting..."); break; case BT_SEARCHING: display.println("Searching V-LINK"); break; case BT_CONNECTED: display.println("Connected"); break; case BT_FAILED: display.println("Retrying..."); break; default: display.println("Idle"); }
  display.display();
}
void drawELM327Status() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setCursor(0, 0); display.println("DPFMonitor"); display.setCursor(0, 18);
  switch (elm327.getState()) { case ELM_SEND_TEMPERATURE_REQUEST: case ELM_WAIT_TEMPERATURE_RESPONSE: display.println("Reading inlet temp"); break; case ELM_SEND_DISTANCE_REQUEST: case ELM_WAIT_DISTANCE_RESPONSE: display.println("Reading distance"); break; case ELM_ERROR: display.println("ELM327 error"); break; default: display.println("Initializing..."); }
  display.display();
}
void drawMonitorScreen() {
  display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); display.setCursor(2, 0); display.print("INLET C"); display.setCursor(68, 0); display.print("SINCE km"); display.drawFastVLine(64, 0, 51, SSD1306_WHITE);
  display.setTextSize(2); display.setCursor(1, 17); if (elm327.hasTemperature()) display.print(elm327.getTemperature(), 1); else display.print("---.-"); display.setCursor(68, 17); if (elm327.hasDistanceSinceRegeneration()) display.print(elm327.getDistanceSinceRegenerationKm(), 1); else display.print("----");
  display.setTextSize(1); display.setCursor(117, 35); if (distanceTrend == TREND_UP) display.print('^'); else if (distanceTrend == TREND_DOWN) display.print('v'); display.setCursor(1, 54); if (regenerationActive) display.print("REGEN ACTIVE"); else if (regenerationCandidate) display.print("CHECKING START"); else display.print("NORMAL"); display.display();
}
void updateDistanceTrend() {
  if (!elm327.hasDistanceSinceRegeneration()) return; uint32_t sampleId = elm327.getDistanceSampleId(); if (sampleId == processedDistanceSampleId) return; processedDistanceSampleId = sampleId; float current = elm327.getDistanceSinceRegenerationKm();
  if (isnan(previousDistanceKm)) { previousDistanceKm = current; return; } float delta = current - previousDistanceKm; if (fabsf(delta) < DISTANCE_NOISE_TOLERANCE_KM) return;
  DistanceTrend observed = delta > 0.0f ? TREND_UP : TREND_DOWN; previousDistanceKm = current; if (observed == pendingTrend) matchingTrendSamples++; else { pendingTrend = observed; matchingTrendSamples = 1; } if (matchingTrendSamples >= TREND_CONFIRMATION_SAMPLES) distanceTrend = pendingTrend;
}
void updateRegenerationLogic() {
  if (!elm327.hasTemperature() || !elm327.hasDistanceSinceRegeneration()) return; float temperature = elm327.getTemperature(); bool startCondition = temperature > REGEN_START_TEMPERATURE && distanceTrend == TREND_DOWN; bool endCondition = temperature < REGEN_END_TEMPERATURE && distanceTrend == TREND_UP;
  if (regenerationActive) { if (!endCondition) { regenerationEndStart = 0; return; } if (!regenerationEndStart) regenerationEndStart = millis(); if (millis() - regenerationEndStart >= REGEN_END_TIME) { regenerationActive = false; regenerationCandidate = false; regenerationEndStart = regenerationCandidateStart = 0; Serial.println("DPF REGENERATION FINISHED"); playRegenerationEndSound(); } return; }
  if (!startCondition) { regenerationCandidate = false; regenerationCandidateStart = 0; return; } if (!regenerationCandidate) { regenerationCandidate = true; regenerationCandidateStart = millis(); Serial.println("Possible DPF regeneration detected..."); } if (millis() - regenerationCandidateStart >= REGEN_START_TIME) { regenerationActive = true; regenerationCandidate = false; regenerationCandidateStart = regenerationEndStart = 0; Serial.println("DPF REGENERATION STARTED"); playRegenerationStartSound(); }
}
void setup() {
  Serial.begin(115200); Wire.begin(21, 22); if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) { Serial.println("ERROR: OLED not found!"); while (true) delay(100); }
#if BUZZER_ENABLED
  ledcAttach(BUZZER_PIN, BUZZER_FREQUENCY, BUZZER_RESOLUTION); buzzerOff();
#endif
  splash.begin(display);
}
void loop() {
  splash.update(); if (splash.isFinished() && !bluetoothStarted) { bluetooth.begin(); bluetoothStarted = true; } if (!bluetoothStarted) return;
  if (!normalOperationStarted) { bluetooth.update(); if (!bluetooth.isConnected()) { drawBluetoothStatus(); return; } if (!bluetoothConnectedSoundPlayed) { playBluetoothConnectedSound(); bluetoothConnectedSoundPlayed = true; } if (!elm327Started) { elm327.begin(bluetooth.stream()); elm327Started = true; } elm327.update(); if (!elm327.isReady()) { drawELM327Status(); return; } normalOperationStarted = true; }
  elm327.update(); updateDistanceTrend(); updateRegenerationLogic(); drawMonitorScreen();
}
