#ifndef ELM327_MANAGER_H
#define ELM327_MANAGER_H

#include <Arduino.h>
#include "BluetoothSerial.h"

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
  ELM_SEND_TEMPERATURE_REQUEST,
  ELM_WAIT_TEMPERATURE_RESPONSE,
  ELM_SEND_DISTANCE_REQUEST,
  ELM_WAIT_DISTANCE_RESPONSE,
  ELM_READY,
  ELM_ERROR
};

class ELM327Manager {
private:
  BluetoothSerial* bt = nullptr;
  ELMState state = ELM_IDLE;
  String response = "";
  String elmVersion = "";

  unsigned long lastDataReceived = 0;
  unsigned long lastRequest = 0;

  const unsigned long responseTimeout = 5000;
  const unsigned long requestInterval = 1000;

  float dpfTemperature = NAN;
  float distanceSinceRegenerationKm = NAN;

  bool temperatureValid = false;
  bool distanceValid = false;
  bool requestDistanceNext = false;

  uint32_t distanceSampleId = 0;

  void sendCommand(const char* command) {
    response = "";
    Serial.print("Sending: ");
    Serial.println(command);
    bt->print(command);
    bt->print("\r");
    lastDataReceived = millis();
  }

  bool responseReceived() {
    return response.indexOf('>') >= 0;
  }

  bool responseContainsError() {
    String text = response;
    text.toUpperCase();

    return text.indexOf("CAN ERROR") >= 0 ||
           text.indexOf("UNABLE TO CONNECT") >= 0 ||
           text.indexOf("STOPPED") >= 0 ||
           text.indexOf("NO DATA") >= 0;
  }

  void printResponseToSerial(const char* title) {
    Serial.println(title);
    Serial.print(response);
    Serial.println();
  }

  static bool parseByte(const String& token, uint8_t& value) {
    if (token.length() != 2) {
      return false;
    }

    char* end;
    long parsed = strtol(token.c_str(), &end, 16);

    if (*end != '\0' || parsed < 0 || parsed > 255) {
      return false;
    }

    value = static_cast<uint8_t>(parsed);
    return true;
  }

  static int parseHexBytes(String line, uint8_t* bytes, int maximum) {
    line.replace("\r", " ");
    line.replace("\n", " ");
    line.replace(">", " ");
    line.replace(":", " ");

    int count = 0;
    int start = 0;

    while (start < line.length() && count < maximum) {
      while (start < line.length() && line[start] == ' ') {
        start++;
      }

      if (start >= line.length()) {
        break;
      }

      int end = line.indexOf(' ', start);

      if (end < 0) {
        end = line.length();
      }

      String token = line.substring(start, end);
      token.trim();

      if (token.length() == 2) {
        uint8_t value;

        if (parseByte(token, value)) {
          bytes[count++] = value;
        }
      } else if (token.length() > 2 && token.length() % 2 == 0) {
        for (int index = 0;
             index < token.length() && count < maximum;
             index += 2) {
          uint8_t value;

          if (!parseByte(token.substring(index, index + 2), value)) {
            break;
          }

          bytes[count++] = value;
        }
      }

      start = end + 1;
    }

    return count;
  }

  // Supports ISO-TP first/consecutive frames and ELM's already reassembled replies.
  int extractUdsPayload(uint8_t* payload, int maximum) {
    int payloadLength = 0;
    int expectedLength = -1;
    int lineStart = 0;

    while (lineStart < response.length() && payloadLength < maximum) {
      int lineEnd = response.indexOf('\n', lineStart);

      if (lineEnd < 0) {
        lineEnd = response.length();
      }

      uint8_t frame[64];

      int frameLength = parseHexBytes(
        response.substring(lineStart, lineEnd),
        frame,
        64
      );

      int dataStart = 0;
      int dataLength = frameLength;

      if (frameLength >= 2 && (frame[0] & 0xF0) == 0x10) {
        expectedLength = ((frame[0] & 0x0F) << 8) | frame[1];
        dataStart = 2;
        dataLength -= 2;
      } else if (
        frameLength &&
        (frame[0] & 0xF0) == 0x20 &&
        expectedLength >= 0
      ) {
        dataStart = 1;
        dataLength--;
      } else if (frameLength && (frame[0] & 0xF0) == 0x00) {
        expectedLength = frame[0] & 0x0F;
        dataStart = 1;
        dataLength--;
      }

      for (int index = 0;
           index < dataLength && payloadLength < maximum;
           index++) {
        if (expectedLength >= 0 && payloadLength >= expectedLength) {
          break;
        }

        payload[payloadLength++] = frame[dataStart + index];
      }

      lineStart = lineEnd + 1;
    }

    return payloadLength;
  }

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

    uint8_t bytes[16];
    int byteCount = 0;
    int start = 0;

    while (start < data.length() && byteCount < 16) {
      while (start < data.length() && data[start] == ' ') {
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
        long value = strtol(token.c_str(), &endPtr, 16);

        if (*endPtr == '\0') {
          bytes[byteCount++] = static_cast<uint8_t>(value);
        }
      }

      start = end + 1;
    }

    if (byteCount < 3) {
      Serial.println("ERROR: Not enough DPF data bytes.");
      return false;
    }

    uint8_t d = bytes[1];
    uint8_t e = bytes[2];
    int rawValue = (static_cast<int>(d) * 256) + static_cast<int>(e);

    dpfTemperature = (static_cast<float>(rawValue) / 10.0f) - 40.0f;
    temperatureValid = true;

    Serial.print("DPF temperature: ");
    Serial.print(dpfTemperature, 1);
    Serial.println(" C");

    return true;
  }

  bool parseDistance() {
    uint8_t payload[128];
    int length = extractUdsPayload(payload, sizeof(payload));
    const int distanceOffset = 45;

    for (int index = 0; index + 3 < length; index++) {
      if (
        payload[index] == 0x62 &&
        payload[index + 1] == 0xED &&
        payload[index + 2] == 0x03
      ) {
        int valueIndex = index + 3 + distanceOffset;

        if (valueIndex + 3 >= length) {
          return false;
        }

        uint32_t raw =
          (static_cast<uint32_t>(payload[valueIndex]) << 24) |
          (static_cast<uint32_t>(payload[valueIndex + 1]) << 16) |
          (static_cast<uint32_t>(payload[valueIndex + 2]) << 8) |
          static_cast<uint32_t>(payload[valueIndex + 3]);

        distanceSinceRegenerationKm =
          static_cast<float>(raw) / 1000.0f;

        distanceValid = true;
        distanceSampleId++;
        return true;
      }
    }

    return false;
  }

  void finishRequest(bool success, bool temperatureRequest) {
    if (!success) {
      if (temperatureRequest) {
        temperatureValid = false;
      } else {
        distanceValid = false;
      }
    }

    lastRequest = millis();
    state = ELM_READY;
  }

public:
  void begin(BluetoothSerial& bluetooth) {
    bt = &bluetooth;
    state = ELM_SEND_ATZ;
    response = "";

    dpfTemperature = NAN;
    distanceSinceRegenerationKm = NAN;

    temperatureValid = false;
    distanceValid = false;
    requestDistanceNext = false;

    distanceSampleId = 0;
    lastDataReceived = millis();
    lastRequest = 0;
  }

  void update() {
    if (bt == nullptr) {
      return;
    }

    bool received = false;

    while (bt->available()) {
      char c = bt->read();
      response += c;
      Serial.write(c);
      received = true;
    }

    if (received) {
      lastDataReceived = millis();
    }

    switch (state) {
      case ELM_SEND_ATZ:
        sendCommand("ATZ");
        state = ELM_WAIT_ATZ;
        break;

      case ELM_WAIT_ATZ:
        if (responseReceived()) {
          state = ELM_SEND_ATI;
        } else if (millis() - lastDataReceived >= responseTimeout) {
          state = ELM_ERROR;
        }
        break;

      case ELM_SEND_ATI:
        sendCommand("ATI");
        state = ELM_WAIT_ATI;
        break;

      case ELM_WAIT_ATI:
        if (responseReceived()) {
          elmVersion = response;
          state = ELM_SEND_ATSP0;
        } else if (millis() - lastDataReceived >= responseTimeout) {
          state = ELM_ERROR;
        }
        break;

      case ELM_SEND_ATSP0:
        sendCommand("ATSP0");
        state = ELM_WAIT_ATSP0;
        break;

      case ELM_WAIT_ATSP0:
        if (responseReceived()) {
          state = ELM_SEND_ATSH;
        } else if (millis() - lastDataReceived >= responseTimeout) {
          state = ELM_ERROR;
        }
        break;

      case ELM_SEND_ATSH:
        sendCommand("ATSH7E0");
        state = ELM_WAIT_ATSH;
        break;

      case ELM_WAIT_ATSH:
        if (responseReceived()) {
          lastRequest = 0;
          state = ELM_SEND_TEMPERATURE_REQUEST;
        } else if (millis() - lastDataReceived >= responseTimeout) {
          state = ELM_ERROR;
        }
        break;

      case ELM_SEND_TEMPERATURE_REQUEST:
        sendCommand("0178");
        state = ELM_WAIT_TEMPERATURE_RESPONSE;
        break;

      case ELM_WAIT_TEMPERATURE_RESPONSE:
        if (responseReceived()) {
          finishRequest(parseTemperature(), true);
        } else if (
          responseContainsError() ||
          millis() - lastDataReceived >= responseTimeout
        ) {
          finishRequest(false, true);
        }
        break;

      case ELM_SEND_DISTANCE_REQUEST:
        sendCommand("22ED03");
        state = ELM_WAIT_DISTANCE_RESPONSE;
        break;

      case ELM_WAIT_DISTANCE_RESPONSE:
        if (responseReceived()) {
          finishRequest(parseDistance(), false);
        } else if (
          responseContainsError() ||
          millis() - lastDataReceived >= responseTimeout
        ) {
          finishRequest(false, false);
        }
        break;

      case ELM_READY:
        if (lastRequest && millis() - lastRequest >= requestInterval) {
          state = requestDistanceNext
            ? ELM_SEND_DISTANCE_REQUEST
            : ELM_SEND_TEMPERATURE_REQUEST;

          requestDistanceNext = !requestDistanceNext;
        }
        break;

      default:
        break;
    }
  }

  ELMState getState() {
    return state;
  }

  String getResponse() {
    return response;
  }

  String getVersion() {
    return elmVersion;
  }

  bool isReady() {
    return state == ELM_READY;
  }

  bool hasError() {
    return state == ELM_ERROR;
  }

  bool hasTemperature() {
    return temperatureValid;
  }

  float getTemperature() {
    return dpfTemperature;
  }

  bool hasDistanceSinceRegeneration() {
    return distanceValid;
  }

  float getDistanceSinceRegenerationKm() {
    return distanceSinceRegenerationKm;
  }

  uint32_t getDistanceSampleId() {
    return distanceSampleId;
  }
};

#endif