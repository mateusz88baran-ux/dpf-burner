#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "SplashScreen.h"
#include "BluetoothManager.h"
#include "ELM327Manager.h"


// ============================================================
// OLED configuration
// ============================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);


// ============================================================
// Application modules
// ============================================================

SplashScreen splash;
BluetoothManager bluetooth;
ELM327Manager elm327;


// ============================================================
// Application states
// ============================================================

bool bluetoothStarted = false;
bool elm327Started = false;
bool normalOperationStarted = false;


// ============================================================
// Regeneration detection
// ============================================================

bool regenerationActive = false;
bool regenerationCandidate = false;

unsigned long regenerationCandidateStart = 0;
unsigned long regenerationEndStart = 0;


// ============================================================
// Regeneration thresholds
// ============================================================

const float REGEN_START_TEMPERATURE = 500.0;
const float REGEN_END_TEMPERATURE = 450.0;


// Temperature must remain above 500 C
// for 15 seconds to confirm regeneration.

const unsigned long REGEN_START_TIME = 15000;


// Temperature must remain below 450 C
// for 30 seconds to finish regeneration.

const unsigned long REGEN_END_TIME = 30000;


// ============================================================
// Buzzer configuration
// ============================================================

// Buzzer is disabled for now.
// Enable it after physically connecting the buzzer.

#define BUZZER_ENABLED false

#define BUZZER_PIN 25


// ============================================================
// Buzzer helper
// ============================================================

void buzzerBeep(
  int frequency,
  int duration
) {

#if BUZZER_ENABLED

  tone(
    BUZZER_PIN,
    frequency,
    duration
  );

#endif
}


// ============================================================
// Regeneration start sound
// ============================================================

void playRegenerationStartSound() {

#if BUZZER_ENABLED

  buzzerBeep(1200, 200);

  delay(250);

  buzzerBeep(1200, 200);

  delay(250);

  buzzerBeep(1600, 400);

#endif
}


// ============================================================
// Regeneration end sound
// ============================================================

void playRegenerationEndSound() {

#if BUZZER_ENABLED

  buzzerBeep(1600, 400);

  delay(300);

  buzzerBeep(1200, 200);

#endif
}


// ============================================================
// Bluetooth status display
// ============================================================

void drawBluetoothStatus() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("DPFMonitor");

  display.setCursor(0, 18);
  display.println("Bluetooth:");

  display.setCursor(0, 30);

  switch (bluetooth.getState()) {

    case BT_STARTING:

      display.println("Starting...");
      break;


    case BT_SEARCHING:

      display.println("Searching V-LINK");
      break;


    case BT_CONNECTED:

      display.println("Connected");
      break;


    case BT_FAILED:

      display.println("Retrying...");
      break;


    default:

      display.println("Idle");
      break;
  }

  display.display();
}


// ============================================================
// ELM327 initialization display
// ============================================================

void drawELM327Status() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("DPFMonitor");


  // Bluetooth status

  display.setCursor(0, 12);
  display.print("BT: ");

  if (bluetooth.isConnected()) {

    display.println("Connected");
  }

  else {

    display.println("Disconnected");
  }


  // ELM327 status

  display.setCursor(0, 25);
  display.println("ELM327:");

  display.setCursor(0, 37);

  switch (elm327.getState()) {

    case ELM_SEND_ATZ:

      display.println("Sending ATZ");
      break;


    case ELM_WAIT_ATZ:

      display.println("Waiting ATZ");
      break;


    case ELM_SEND_ATI:

      display.println("Sending ATI");
      break;


    case ELM_WAIT_ATI:

      display.println("Waiting ATI");
      break;


    case ELM_SEND_ATSP0:

      display.println("Sending ATSP0");
      break;


    case ELM_WAIT_ATSP0:

      display.println("Waiting ATSP0");
      break;


    case ELM_SEND_ATSH:

      display.println("Sending ATSH7E0");
      break;


    case ELM_WAIT_ATSH:

      display.println("Waiting ATSH7E0");
      break;


    case ELM_SEND_DPF_REQUEST:

      display.println("Sending 0178");
      break;


    case ELM_WAIT_DPF_RESPONSE:

      display.println("Waiting ECU...");
      break;


    case ELM_READY:

      display.println("READY");
      break;


    case ELM_ERROR:

      display.println("ERROR");
      break;


    default:

      display.println("IDLE");
      break;
  }

  display.display();
}


// ============================================================
// Temperature display
// ============================================================

void drawTemperatureScreen() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);


  // ----------------------------------------------------------
  // Title
  // ----------------------------------------------------------

  display.setTextSize(1);

  display.setCursor(0, 0);

  display.println("DPF INLET TEMP");


  // ----------------------------------------------------------
  // Temperature
  // ----------------------------------------------------------

  display.setTextSize(2);

  display.setCursor(0, 16);

  if (elm327.hasTemperature()) {

    display.print(
      elm327.getTemperature(),
      1
    );

    display.println(" C");
  }

  else {

    display.println("---.- C");
  }


  // ----------------------------------------------------------
  // Regeneration status
  // ----------------------------------------------------------

  display.setTextSize(1);

  display.setCursor(0, 43);


  if (regenerationActive) {

    display.println("DPF REGENERATION");
  }

  else if (regenerationCandidate) {

    display.println("CHECKING REGEN");
  }

  else {

    display.println("NORMAL");
  }


  display.display();
}


// ============================================================
// Regeneration detection logic
// ============================================================

void updateRegenerationLogic() {

  if (!elm327.hasTemperature()) {

    return;
  }


  float temperature = elm327.getTemperature();


  // ==========================================================
  // Regeneration is currently active
  // ==========================================================

  if (regenerationActive) {


    // --------------------------------------------------------
    // Temperature dropped below the end threshold
    // --------------------------------------------------------

    if (temperature < REGEN_END_TEMPERATURE) {

      if (regenerationEndStart == 0) {

        regenerationEndStart = millis();
      }


      // Temperature must remain below 450 C
      // for 30 seconds.

      if (
        millis() - regenerationEndStart >=
        REGEN_END_TIME
      ) {

        regenerationActive = false;

        regenerationCandidate = false;

        regenerationEndStart = 0;
        regenerationCandidateStart = 0;


        Serial.println();
        Serial.println("==============================");
        Serial.println("DPF REGENERATION FINISHED");
        Serial.println("==============================");
        Serial.println();


        playRegenerationEndSound();
      }
    }

    else {

      // Temperature returned above the end threshold.

      regenerationEndStart = 0;
    }


    return;
  }


  // ==========================================================
  // Regeneration is not active
  // ==========================================================

  if (temperature > REGEN_START_TEMPERATURE) {


    // --------------------------------------------------------
    // Start regeneration candidate
    // --------------------------------------------------------

    if (!regenerationCandidate) {

      regenerationCandidate = true;

      regenerationCandidateStart = millis();


      Serial.println();
      Serial.println(
        "Possible DPF regeneration detected..."
      );

      Serial.println(
        "Temperature is above 500 C."
      );
    }


    // --------------------------------------------------------
    // Confirm regeneration after 15 seconds
    // --------------------------------------------------------

    if (
      millis() - regenerationCandidateStart >=
      REGEN_START_TIME
    ) {

      regenerationActive = true;

      regenerationCandidate = false;

      regenerationCandidateStart = 0;
      regenerationEndStart = 0;


      Serial.println();
      Serial.println("==============================");
      Serial.println("DPF REGENERATION STARTED");
      Serial.println("==============================");
      Serial.println();


      playRegenerationStartSound();
    }
  }

  else {


    // --------------------------------------------------------
    // Temperature dropped below 500 C
    // before regeneration was confirmed.
    // --------------------------------------------------------

    if (regenerationCandidate) {

      Serial.println(
        "DPF regeneration candidate cancelled."
      );
    }


    regenerationCandidate = false;

    regenerationCandidateStart = 0;
  }
}


// ============================================================
// Setup
// ============================================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("DPFMonitor starting...");
  Serial.println();


  // ----------------------------------------------------------
  // Initialize I2C
  // ----------------------------------------------------------

  Wire.begin(21, 22);


  // ----------------------------------------------------------
  // Initialize OLED
  // ----------------------------------------------------------

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      OLED_ADDRESS
    )
  ) {

    Serial.println(
      "ERROR: OLED not found!"
    );

    while (true) {

      delay(100);
    }
  }


  Serial.println("OLED initialized");


  // ----------------------------------------------------------
  // Initialize buzzer pin
  // ----------------------------------------------------------

#if BUZZER_ENABLED

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

#endif


  // ----------------------------------------------------------
  // Start splash screen
  // ----------------------------------------------------------

  splash.begin(display);
}


// ============================================================
// Main loop
// ============================================================

void loop() {


  // ==========================================================
  // Update splash screen
  // ==========================================================

  splash.update();


  // ==========================================================
  // Start Bluetooth after splash screen
  // ==========================================================

  if (
    splash.isFinished() &&
    !bluetoothStarted
  ) {

    Serial.println(
      "Starting Bluetooth manager..."
    );


    bluetooth.begin();

    bluetoothStarted = true;
  }


  // ==========================================================
  // Wait for Bluetooth startup
  // ==========================================================

  if (!bluetoothStarted) {

    return;
  }


  // ==========================================================
  // Bluetooth connection phase
  // ==========================================================

  if (!normalOperationStarted) {

    bluetooth.update();


    // --------------------------------------------------------
    // Wait until Bluetooth is connected
    // --------------------------------------------------------

    if (!bluetooth.isConnected()) {

      drawBluetoothStatus();

      return;
    }


    // --------------------------------------------------------
    // Bluetooth connection established
    // --------------------------------------------------------

    Serial.println(
      "Bluetooth connection established."
    );


    // --------------------------------------------------------
    // Start ELM327 initialization
    // --------------------------------------------------------

    if (!elm327Started) {

      elm327.begin(
        bluetooth.stream()
      );

      elm327Started = true;
    }


    // --------------------------------------------------------
    // Update ELM327 initialization
    // --------------------------------------------------------

    elm327.update();


    if (!elm327.isReady()) {

      drawELM327Status();

      return;
    }


    // --------------------------------------------------------
    // ELM327 initialization completed
    // --------------------------------------------------------

    Serial.println(
      "ELM327 initialization completed."
    );


    normalOperationStarted = true;
  }


  // ==========================================================
  // Normal operation
  // ==========================================================

  elm327.update();


  // ==========================================================
  // Update regeneration detection
  // ==========================================================

  updateRegenerationLogic();


  // ==========================================================
  // Show temperature screen
  // ==========================================================

  drawTemperatureScreen();
}