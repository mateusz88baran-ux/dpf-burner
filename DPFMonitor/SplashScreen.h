#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class SplashScreen {
  private:
    Adafruit_SSD1306* display;

    unsigned long startTime = 0;
    unsigned long lastFrameTime = 0;

    static const unsigned long DURATION = 3000;     // splash: 3 sekundy
    static const unsigned long FRAME_TIME = 180;    // zmiana ognia co 180 ms

    uint8_t frame = 0;
    bool active = false;

    // Proste klatki animacji ognia.
    // Każdy znak '#' oznacza piksel.
    const char* fireFrames[3][8] = {
      {
        "    ##    ",
        "   ####   ",
        "  ######  ",
        "  ######  ",
        " #######  ",
        "######### ",
        "##########",
        " ######## "
      },
      {
        "    ##    ",
        "   ####   ",
        "  ######  ",
        " ### #### ",
        "######### ",
        "##########",
        " #########",
        "   ####   "
      },
      {
        "   ###    ",
        "  #####   ",
        "  ######  ",
        " #### ####",
        "######### ",
        " #########",
        "##########",
        "  ######  "
      }
    };

    void drawFire() {
      const int pixelSize = 2;
      const int fireWidth = 10 * pixelSize;
      const int fireHeight = 8 * pixelSize;

      const int startX = (128 - fireWidth) / 2;
      const int startY = 2;

      for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 10; x++) {
          if (fireFrames[frame][y][x] == '#') {
            display->fillRect(
              startX + x * pixelSize,
              startY + y * pixelSize,
              pixelSize,
              pixelSize,
              SSD1306_WHITE
            );
          }
        }
      }
    }

    void drawTitle() {
      display->setTextColor(SSD1306_WHITE);
      display->setTextSize(2);

      const char* title = "DPFMonitor";

      int16_t x1, y1;
      uint16_t w, h;

      display->getTextBounds(
        title,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
      );

      int x = (128 - w) / 2;

      display->setCursor(x, 30);
      display->print(title);

      display->setTextSize(1);

      const char* subtitle = "Initializing...";

      display->getTextBounds(
        subtitle,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
      );

      x = (128 - w) / 2;

      display->setCursor(x, 52);
      display->print(subtitle);
    }

  public:

    void begin(Adafruit_SSD1306& oled) {
      display = &oled;

      startTime = millis();
      lastFrameTime = startTime;

      frame = 0;
      active = true;

      draw();
    }

    void update() {
      if (!active) {
        return;
      }

      unsigned long now = millis();

      // Koniec splashscreena
      if (now - startTime >= DURATION) {
        active = false;

        display->clearDisplay();
        display->display();

        return;
      }

      // Zmiana klatki animacji
      if (now - lastFrameTime >= FRAME_TIME) {
        lastFrameTime = now;

        frame++;

        if (frame >= 3) {
          frame = 0;
        }

        draw();
      }
    }

    bool isFinished() {
      return !active;
    }

    void draw() {
      display->clearDisplay();

      drawFire();
      drawTitle();

      display->display();
    }
};

#endif