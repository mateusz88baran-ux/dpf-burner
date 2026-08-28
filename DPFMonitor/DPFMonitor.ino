#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "SplashScreen.h"
#include "BluetoothManager.h"
#include "ELM327Manager.h"


// ==============================
// OLED configuration
// ==============================

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


// ==============================
// Application modules
// ==============================

SplashScreen splash;
BluetoothManager bluetooth;
ELM327Manager elm327;


// ==============================
// Application states
// ==============================

bool bluetoothStarted = false;
bool elm327Started = false;


// ==============================
// Bluetooth status display
// ==============================

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


// ==============================
// ELM327 status display
// ==============================

void drawELM327Status() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);


  display.setCursor(0, 0);
  display.println("DPFMonitor");


  display.setCursor(0, 12);
  display.print("BT: ");

  if (bluetooth.isConnected()) {

    display.println("Connected");
  }
  else {

    display.println("Disconnected");
  }


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


// ==============================
// DPF temperature display
// ==============================

void drawDPFTemperature() {

  display.clearDisplay();

  display.setTextColor(SSD1306_WHITE);


  display.setTextSize(1);

  display.setCursor(0, 0);

  display.println("DPF INLET TEMP");


  if (elm327.hasTemperature()) {

    display.setTextSize(2);

    display.setCursor(0, 20);

    display.print(elm327.getTemperature(), 1);

    display.println(" C");

  }
  else {

    display.setTextSize(1);

    display.setCursor(0, 25);

    display.println("NO VALID DATA");
  }


  display.display();
}


// ==============================
// Setup
// ==============================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("DPFMonitor starting...");


  // Initialize I2C
  Wire.begin(21, 22);


  // Initialize OLED
  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
      )) {

    Serial.println("ERROR: OLED not found!");

    while (true) {
      delay(100);
    }
  }


  Serial.println("OLED initialized");


  // Start splash screen
  splash.begin(display);
}


// ==============================
// Main loop
// ==============================

void loop() {

  // Update splash animation
  splash.update();


  // Start Bluetooth after splash screen
  if (
    splash.isFinished() &&
    !bluetoothStarted
  ) {

    Serial.println("Starting Bluetooth manager...");

    bluetooth.begin();

    bluetoothStarted = true;
  }


  // Wait for Bluetooth initialization
  if (!bluetoothStarted) {

    return;
  }


  // Update Bluetooth
  bluetooth.update();


  // Bluetooth not connected
  if (!bluetooth.isConnected()) {

    drawBluetoothStatus();

    return;
  }


  // Initialize ELM327 once Bluetooth is connected
  if (!elm327Started) {

    elm327.begin(bluetooth.stream());

    elm327Started = true;
  }


  // Process ELM327 state machine
  elm327.update();


  // Show temperature after a successful response
  if (elm327.getState() == ELM_READY) {

    drawDPFTemperature();
  }
  else {

    drawELM327Status();
  }
}