#include "Display.h"

Display::Display()
  : display(0x3c, D3, D5), lastSuccessfulWrite(0) {
}

void Display::recoverI2CBus() {
  // Wire.end() não existe no core ESP8266; manipulamos os pinos diretamente
  pinMode(D5, OUTPUT);        // SCL
  pinMode(D3, INPUT_PULLUP);  // SDA

  // 9 pulsos de clock para destravar slave com SDA preso em LOW
  for (int i = 0; i < 9; i++) {
    digitalWrite(D5, LOW);
    delayMicroseconds(5);
    digitalWrite(D5, HIGH);
    delayMicroseconds(5);
    if (digitalRead(D3) == HIGH) break;
  }

  // Condição de STOP: SDA sobe enquanto SCL está HIGH
  pinMode(D3, OUTPUT);
  digitalWrite(D3, LOW);
  delayMicroseconds(5);
  digitalWrite(D5, HIGH);
  delayMicroseconds(5);
  digitalWrite(D3, HIGH);
  delayMicroseconds(5);

  // Reentrega os pinos ao Wire
  Wire.begin(D3, D5);
}

void Display::reinit() {
  recoverI2CBus();
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
}

void Display::healthCheck() {
  unsigned long now = millis();
  if (now - lastSuccessfulWrite > DISPLAY_WATCHDOG_MS) {
    Serial.println("[DISPLAY] Watchdog: reinicializando display");
    reinit();
    lastSuccessfulWrite = now;
  }
}

void Display::begin() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  display.clear();
  display.display();
  lastSuccessfulWrite = millis();
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
  lastSuccessfulWrite = millis();

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
  lastSuccessfulWrite = millis();
}
