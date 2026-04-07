#ifndef Display_h
#define Display_h

#include <Wire.h>
#include "SSD1306Wire.h"
#include "Fonts.h"

class Display {
  public:
    Display();
    void begin();
    void showWelcome(const String &line1, const String &line2, const String &line3, const String &line4);
    void showFilling(const String &line1, const String &line2, const String &line3, const String &line4);
    void healthCheck();
    bool needsRepaint;
  private:
    static const unsigned long DISPLAY_WATCHDOG_MS = 10000;
    SSD1306Wire display;
    unsigned long lastSuccessfulWrite;
    uint8_t consecutiveI2CFailures;
    bool isI2CBusOk();
    void recoverI2CBus();
    void reinit();
};

#endif
