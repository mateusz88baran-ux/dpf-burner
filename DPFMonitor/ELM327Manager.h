#ifndef ELM327_MANAGER_H
#define ELM327_MANAGER_H

#include <Arduino.h>
#include "BluetoothSerial.h"


// ============================================================
// ELM327 states
// ============================================================

enum ELMState {

  ELM_IDLE,

  ELM_SEND_ATZ,
  ELM_WAIT_ATZ,

  ELM_SEND_ATI,
  ELM_WAIT_ATI,

  ELM_SEND_ATSP0,
  ELM_WAIT_ATSP0,

  ELM_SEND_ATSH,
  ELM_WAIT_ATSH,

  ELM_SEND_DPF_REQUEST,
  ELM_WAIT_DPF_RESPONSE,

  ELM_READY,

  ELM_ERROR
};


// ============================================================
// ELM327 Manager
// ============================================================

class ELM327Manager {

private:

  BluetoothSerial* bt = nullptr;

  ELMState state = ELM_IDLE;


  // ----------------------------------------------------------
  // Response data
  // ----------------------------------------------------------

  String response = "";

  String elmVersion = "";


  // ----------------------------------------------------------
  // Timing
  // ----------------------------------------------------------

  unsigned long lastDataReceived = 0;
  unsigned long lastRequest = 0;


  // Maximum time without receiving new data
  const unsigned long responseTimeout = 5000;


  // Time between DPF requests
  const unsigned long requestInterval = 1000;


  // ----------------------------------------------------------
  // Temperature
  // ----------------------------------------------------------

  float dpfTemperature = NAN;

  bool temperatureValid = false;


  // ==========================================================
  // Send command
  // ==========================================================

  void sendCommand(const char* command) {

    response = "";

    Serial.println();
    Serial.print("Sending: ");
    Serial.println(command);

    bt->print(command);
    bt->print("\r");

    lastDataReceived = millis();
  }


  // ==========================================================
  // Check ELM327 prompt
  // ==========================================================

  bool responseReceived() {

    return response.indexOf('>') >= 0;
  }


  // ==========================================================
  // Check for ELM327 errors
  // ==========================================================

  bool responseContainsError() {

    String upperResponse = response;

    upperResponse.toUpperCase();

    return (
      upperResponse.indexOf("CAN ERROR") >= 0 ||
      upperResponse.indexOf("UNABLE TO CONNECT") >= 0 ||
      upperResponse.indexOf("STOPPED") >= 0
    );
  }


  // ==========================================================
  // Print response to Serial
  // ==========================================================

  void printResponseToSerial(const char* title) {

    Serial.println();
    Serial.println("==============================");
    Serial.println(title);
    Serial.println("------------------------------");
    Serial.print(response);
    Serial.println();
    Serial.println("==============================");
    Serial.println();
  }


  // ==========================================================
  // Parse DPF temperature
  // ==========================================================

  bool parseTemperature() {

    String cleaned = response;

    cleaned.replace("\r", " ");
    cleaned.replace("\n", " ");
    cleaned.replace(">", " ");

    cleaned.toUpperCase();

    int pos = cleaned.indexOf("41 78");

    if (pos < 0) {

      Serial.println("ERROR: 41 78 response not found.");

      return false;
    }


    String data = cleaned.substring(pos + 5);

    data.trim();


    Serial.print("DPF data after PID: ");
    Serial.println(data);


    // --------------------------------------------------------
    // Extract hexadecimal bytes
    // --------------------------------------------------------

    uint8_t bytes[16];

    int byteCount = 0;

    int start = 0;


    while (
      start < data.length() &&
      byteCount < 16
    ) {

      while (
        start < data.length() &&
        data[start] == ' '
      ) {

        start++;
      }


      if (start >= data.length()) {

        break;
      }


      int end = data.indexOf(' ', start);


      if (end < 0) {

        end = data.length();
      }


      String token = data.substring(start, end);

      token.trim();


      if (token.length() == 2) {

        char* endPtr;

        long value = strtol(
          token.c_str(),
          &endPtr,
          16
        );


        if (*endPtr == '\0') {

          bytes[byteCount] = (uint8_t)value;

          byteCount++;
        }
      }


      start = end + 1;
    }


    // --------------------------------------------------------
    // Expected data:
    //
    // 41 78 00 05 DC 05 DC
    //
    // D = 05
    // E = DC
    // --------------------------------------------------------

    if (byteCount < 3) {

      Serial.println("ERROR: Not enough DPF data bytes.");

      return false;
    }


    uint8_t d = bytes[1];

    uint8_t e = bytes[2];


    int rawValue =
      ((int)d * 256) +
      (int)e;


    // Formula:
    //
    // ((D * 256 + E) / 10) - 40

    dpfTemperature =
      ((float)rawValue / 10.0f) -
      40.0f;


    temperatureValid = true;


    // --------------------------------------------------------
    // Debug information
    // --------------------------------------------------------

    Serial.println();
    Serial.println("DPF TEMPERATURE");
    Serial.println("------------------------------");

    Serial.print("D = 0x");

    if (d < 0x10) {

      Serial.print("0");
    }

    Serial.println(d, HEX);


    Serial.print("E = 0x");

    if (e < 0x10) {

      Serial.print("0");
    }

    Serial.println(e, HEX);


    Serial.print("Raw value = ");
    Serial.println(rawValue);


    Serial.print("Temperature = ");
    Serial.print(dpfTemperature, 1);
    Serial.println(" C");


    Serial.println("------------------------------");
    Serial.println();


    return true;
  }


public:


  // ==========================================================
  // Begin
  // ==========================================================

  void begin(BluetoothSerial& bluetooth) {

    bt = &bluetooth;

    state = ELM_SEND_ATZ;

    response = "";

    elmVersion = "";

    dpfTemperature = NAN;

    temperatureValid = false;

    lastDataReceived = millis();

    lastRequest = 0;


    Serial.println();
    Serial.println("ELM327 initialization started");
    Serial.println();
  }


  // ==========================================================
  // Main state machine
  // ==========================================================

  void update() {

    if (bt == nullptr) {

      return;
    }


    // ========================================================
    // Read Bluetooth data
    // ========================================================

    bool receivedData = false;


    while (bt->available()) {

      char c = bt->read();

      response += c;

      Serial.write(c);

      receivedData = true;
    }


    // Reset timeout whenever data is received

    if (receivedData) {

      lastDataReceived = millis();
    }


    // ========================================================
    // State machine
    // ========================================================

    switch (state) {


      // ======================================================
      // ATZ
      // ======================================================

      case ELM_SEND_ATZ:

        sendCommand("ATZ");

        state = ELM_WAIT_ATZ;

        break;


      case ELM_WAIT_ATZ:

        if (responseReceived()) {

          printResponseToSerial("ATZ RESPONSE");

          state = ELM_SEND_ATI;
        }

        else if (
          millis() - lastDataReceived >= responseTimeout
        ) {

          printResponseToSerial("ATZ TIMEOUT");

          state = ELM_ERROR;
        }

        break;


      // ======================================================
      // ATI
      // ======================================================

      case ELM_SEND_ATI:

        sendCommand("ATI");

        state = ELM_WAIT_ATI;

        break;


      case ELM_WAIT_ATI:

        if (responseReceived()) {

          elmVersion = response;

          printResponseToSerial("ATI RESPONSE");

          state = ELM_SEND_ATSP0;
        }

        else if (
          millis() - lastDataReceived >= responseTimeout
        ) {

          printResponseToSerial("ATI TIMEOUT");

          state = ELM_ERROR;
        }

        break;


      // ======================================================
      // ATSP0
      // ======================================================

      case ELM_SEND_ATSP0:

        sendCommand("ATSP0");

        state = ELM_WAIT_ATSP0;

        break;


      case ELM_WAIT_ATSP0:

        if (responseReceived()) {

          printResponseToSerial("ATSP0 RESPONSE");

          state = ELM_SEND_ATSH;
        }

        else if (
          millis() - lastDataReceived >= responseTimeout
        ) {

          printResponseToSerial("ATSP0 TIMEOUT");

          state = ELM_ERROR;
        }

        break;


      // ======================================================
      // ATSH7E0
      // ======================================================

      case ELM_SEND_ATSH:

        sendCommand("ATSH7E0");

        state = ELM_WAIT_ATSH;

        break;


      case ELM_WAIT_ATSH:

        if (responseReceived()) {

          printResponseToSerial("ATSH7E0 RESPONSE");

          lastRequest = 0;

          state = ELM_SEND_DPF_REQUEST;
        }

        else if (
          millis() - lastDataReceived >= responseTimeout
        ) {

          printResponseToSerial("ATSH7E0 TIMEOUT");

          state = ELM_ERROR;
        }

        break;


      // ======================================================
      // Send DPF request
      // ======================================================

      case ELM_SEND_DPF_REQUEST:

        sendCommand("0178");

        state = ELM_WAIT_DPF_RESPONSE;

        break;


      // ======================================================
      // Wait for DPF response
      // ======================================================

      case ELM_WAIT_DPF_RESPONSE:

        if (responseReceived()) {

          printResponseToSerial(
            "DPF 0178 RESPONSE"
          );


          if (parseTemperature()) {

            Serial.print("DPF temperature: ");
            Serial.print(dpfTemperature, 1);
            Serial.println(" C");
          }

          else {

            temperatureValid = false;
          }


          lastRequest = millis();

          state = ELM_READY;
        }


        else if (responseContainsError()) {

          printResponseToSerial(
            "DPF 0178 ERROR RESPONSE"
          );

          temperatureValid = false;

          lastRequest = millis();

          state = ELM_READY;
        }


        else if (
          millis() - lastDataReceived >= responseTimeout
        ) {

          printResponseToSerial(
            "DPF 0178 TIMEOUT"
          );

          temperatureValid = false;

          lastRequest = millis();

          state = ELM_READY;
        }

        break;


      // ======================================================
      // Ready
      // ======================================================

      case ELM_READY:

        if (
          lastRequest != 0 &&
          millis() - lastRequest >= requestInterval
        ) {

          state = ELM_SEND_DPF_REQUEST;
        }

        break;


      // ======================================================
      // Error
      // ======================================================

      case ELM_ERROR:

        break;


      default:

        break;
    }
  }


  // ==========================================================
  // Get state
  // ==========================================================

  ELMState getState() {

    return state;
  }


  // ==========================================================
  // Get raw response
  // ==========================================================

  String getResponse() {

    return response;
  }


  // ==========================================================
  // Get ELM327 version
  // ==========================================================

  String getVersion() {

    return elmVersion;
  }


  // ==========================================================
  // Check ready state
  // ==========================================================

  bool isReady() {

    return state == ELM_READY;
  }


  // ==========================================================
  // Check error state
  // ==========================================================

  bool hasError() {

    return state == ELM_ERROR;
  }


  // ==========================================================
  // Check temperature validity
  // ==========================================================

  bool hasTemperature() {

    return temperatureValid;
  }


  // ==========================================================
  // Get DPF temperature
  // ==========================================================

  float getTemperature() {

    return dpfTemperature;
  }
};


#endif