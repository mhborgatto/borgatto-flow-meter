#include "Http.h"
#include "Config.h"
#include <ESP8266WiFi.h>

extern String descricao;
extern int codCliente;
extern double flowMilliLitres;
extern double totalValue;
extern bool servingDisplayFrozen;
extern double frozenServingMl;
extern double frozenServingValue;

void Http::sendReport() {
  if (config.webhookUrl[0] == '\0') {
    Serial.println("[HTTP] Webhook URL não configurada");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println("[HTTP] Enviando relatório de consumo...");
  http.begin(client, String(config.webhookUrl));
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  double reportMl = servingDisplayFrozen ? frozenServingMl : flowMilliLitres;
  double reportVal = servingDisplayFrozen ? frozenServingValue : totalValue;

  StaticJsonDocument<256> doc;
  doc["deviceId"] = config.deviceId;
  doc["codCliente"] = codCliente;
  doc["volumeMl"] = reportMl;
  doc["valor"] = reportVal;
  doc["descricao"] = descricao.length() > 0 ? descricao : "Chopp";
  doc["uptimeMs"] = millis();

  serializeJson(doc, jsonOutput, sizeof(jsonOutput));

  int httpCode = http.POST(String(jsonOutput));

  if (httpCode > 0) {
    Serial.printf("[HTTP] POST code: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("[HTTP] OK");
    }
  } else {
    Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
