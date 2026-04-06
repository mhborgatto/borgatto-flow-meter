#ifndef Display_h
#define Display_h

#include <Wire.h>
#include "SSD1306Wire.h"
#include "Fonts.h"

class Display {
  public:
    Display();
    void begin();
    void showWelcome(String line1, String line2, String line3, String line4);
    void showFilling(String line1, String line2, String line3, String line4);
    void healthCheck();
  private:
    static const unsigned long DISPLAY_WATCHDOG_MS = 30000;
    SSD1306Wire display;
    unsigned long lastSuccessfulWrite;
    void recoverI2CBus();
    void reinit();
};

#endif
