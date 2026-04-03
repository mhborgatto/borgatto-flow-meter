#include "Display.h"

Display::Display()
  : display(0x3c, D3, D5) {
}

void Display::begin() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  display.clear();
  display.display();
}

void Display::showWelcome(String line1, String line2, String line3, String line4) {
  display.clear();
  display.setFont(Cousine_Regular_10);
  display.drawString(0, 0, line1);
  display.setFont(Cousine_Regular_12);
  display.drawString(0, 15, line2);
  display.setFont(Cousine_Regular_12);
  display.drawString(0, 30, line3);
  display.setFont(Cousine_Regular_12);
  display.drawString(0, 45, line4);
  display.display();

  delay(2000);
}

void Display::showFilling(String line1, String line2, String line3, String line4) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(Cousine_Regular_10);
  display.drawString(0, 0, line1);
  display.setFont(Cousine_Regular_16);
  display.drawString(0, 15, line2);
  display.setFont(Cousine_Regular_14);
  display.drawString(0, 30, line3);
  display.drawString(0, 45, line4);
  display.display();
}