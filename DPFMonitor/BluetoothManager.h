#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <Arduino.h>
#include "BluetoothSerial.h"


enum BluetoothState {

  BT_IDLE,
  BT_STARTING,
  BT_SEARCHING,
  BT_CONNECTED,
  BT_FAILED

};


class BluetoothManager {

private:

  BluetoothSerial bt;

  const char* deviceName = "V-LINK";
  const char* pin = "1234";

  BluetoothState state = BT_IDLE;

  unsigned long lastAttempt = 0;

  const unsigned long retryInterval = 5000;

  bool initialized = false;


public:

  void begin() {

    Serial.println("Starting Bluetooth...");

    state = BT_STARTING;


    if (!bt.begin("DPFMonitor", true)) {

      Serial.println("Bluetooth initialization failed");

      state = BT_FAILED;

      return;
    }


    if (!bt.setPin(pin, 4)) {

      Serial.println("WARNING: Bluetooth PIN setup failed");
    }


    initialized = true;

    state = BT_SEARCHING;

    Serial.println("Bluetooth ready");
  }


  void update() {

    if (!initialized) {
      return;
    }


    unsigned long now = millis();


    // Already connected
    if (state == BT_CONNECTED) {

      if (!bt.connected()) {

        Serial.println("Bluetooth disconnected");

        state = BT_FAILED;

        lastAttempt = now;
      }

      return;
    }


    // Wait before trying again
    if (now - lastAttempt < retryInterval) {
      return;
    }


    lastAttempt = now;

    state = BT_SEARCHING;

    Serial.println("Searching for V-LINK...");


    if (bt.connect(deviceName)) {

      Serial.println("Connected to V-LINK");

      state = BT_CONNECTED;
    }
    else {

      Serial.println("Connection failed");

      state = BT_FAILED;
    }
  }


  BluetoothState getState() {

    return state;
  }


  bool isConnected() {

    return state == BT_CONNECTED;
  }


  BluetoothSerial& stream() {

    return bt;
  }

};


#endif