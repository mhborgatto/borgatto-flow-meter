#include "WebServer.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

static const char *const kWebAdminUser = "admin";
static const char *const kWebAdminPass = "ADMBORGATTO";

extern unsigned long flowMilliLitres;
extern double totalValue;
extern String descricao;
extern int codCliente;
extern float conversionFactor;

String WebServer::buildConfigPage() {
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>FlowMeter Config</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;max-width:600px;margin:0 auto;padding:20px;background:#f5f5f5}"
    "h1{color:#333;border-bottom:2px solid #007bff;padding-bottom:10px}"
    ".s{background:#fff;padding:20px;margin-top:15px;border-radius:8px;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
    "h2{color:#007bff;margin-top:0}"
    "label{display:block;margin-top:12px;font-weight:bold;color:#555}"
    "input[type=text],input[type=password],input[type=number]{width:100%;padding:8px;margin-top:4px;"
    "border:1px solid #ccc;border-radius:4px;box-sizing:border-box}"
    ".btn{margin-top:20px;padding:12px 24px;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:16px;width:100%}"
    ".btn-save{background:#007bff}.btn-save:hover{background:#0056b3}"
    ".btn-rst{background:#dc3545;margin-top:10px}.btn-rst:hover{background:#a71d2a}"
    ".ok{background:#d4edda;color:#155724;padding:12px;border-radius:4px;margin-top:10px}"
    "</style></head><body>");

  html += F("<h1>");
  html += String(config.brandName);
  html += F(" - Config</h1>");

  html += F("<form method='POST' action='/config'>");

  html += F("<div class='s'><h2>Dispositivo</h2>");
  html += F("<label>ID do Dispositivo</label>");
  html += F("<input type='text' name='deviceId' value='");
  html += String(config.deviceId);
  html += F("' maxlength='31'>");
  html += F("<label>Nome da Marca (título desta página web)</label>");
  html += F("<input type='text' name='brandName' value='");
  html += String(config.brandName);
  html += F("' maxlength='31'>");
  html += F("<label>Título no display OLED (linha de cima)</label>");
  html += F("<small style='color:#666'>Se vazio, usa o mesmo texto que &quot;Nome da Marca&quot;.</small>");
  html += F("<input type='text' name='oledTitle' value='");
  html += String(config.oledTitle);
  html += F("' maxlength='31' placeholder='Ex.: Borgatto Tap'>");
  html += F("<label>Token de Segurança</label>");
  html += F("<input type='text' name='deviceToken' value='");
  html += String(config.deviceToken);
  html += F("' maxlength='63'>");
  html += F("</div>");

  html += F("<div class='s'><h2>MQTT</h2>");
  html += F("<label>Broker</label>");
  html += F("<input type='text' name='mqttBroker' value='");
  html += String(config.mqttBroker);
  html += F("' maxlength='63'>");
  html += F("<label>Porta</label>");
  html += F("<input type='number' name='mqttPort' value='");
  html += String(config.mqttPort);
  html += F("'>");
  html += F("<label>Usuário</label>");
  html += F("<input type='text' name='mqttUser' value='");
  html += String(config.mqttUser);
  html += F("' maxlength='31'>");
  html += F("<label>Senha</label>");
  html += F("<input type='password' name='mqttPass' value='");
  html += String(config.mqttPass);
  html += F("' maxlength='31'>");
  html += F("<label>Tópico Base</label>");
  html += F("<input type='text' name='mqttTopicBase' value='");
  html += String(config.mqttTopicBase);
  html += F("' maxlength='63'>");
  html += F("</div>");

  html += F("<div class='s'><h2>HTTP / Webhook</h2>");
  html += F("<label>URL do Webhook</label>");
  html += F("<input type='text' name='webhookUrl' value='");
  html += String(config.webhookUrl);
  html += F("' maxlength='127'>");
  html += F("</div>");

  html += F("<div class='s'><h2>Calibração</h2>");
  html += F("<label>Fator de Conversão Padrão</label>");
  html += F("<input type='text' name='defaultConvFactor' value='");
  html += String(config.defaultConvFactor, 4);
  html += F("'>");
  html += F("<label>Debounce da Válvula (ms)</label>");
  html += F("<input type='number' name='valveDebounceMs' value='");
  html += String(config.valveDebounceMs);
  html += F("'>");
  html += F("</div>");

  html += F("<input type='submit' value='Salvar Configuração' class='btn btn-save'>");
  html += F("</form>");

  html += F("<form method='POST' action='/restart'>");
  html += F("<input type='submit' value='Reiniciar Dispositivo' class='btn btn-rst'>");
  html += F("</form>");

  html += F("<div class='s'><h2>Rede WiFi</h2>");
  html += F("<p>Reinicia e abre o AP <strong>");
  html += FLOWMETER_WIFI_AP_NAME;
  html += F("</strong> (senha no firmware) para alterar o WiFi em <strong>192.168.4.1</strong>.</p>");
  html += F("<form method='POST' action='/wifi-portal'>");
  html += F("<input type='submit' value='Abrir portal WiFi' class='btn' style='background:#6c757d;margin-top:10px'>");
  html += F("</form></div>");

  html += F("</body></html>");
  return html;
}

bool WebServer::checkAuth() {
  if (!server.authenticate(kWebAdminUser, kWebAdminPass)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void WebServer::handleRoot() {
  if (!checkAuth()) {
    return;
  }
  server.send(200, "text/html", buildConfigPage());
}

void WebServer::handleConfig() {
  if (!checkAuth()) {
    return;
  }
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  auto copyArg = [&](const char *name, char *dest, size_t maxLen) {
    if (server.hasArg(name)) {
      String val = server.arg(name);
      memset(dest, 0, maxLen);
      val.toCharArray(dest, maxLen);
    }
  };

  copyArg("deviceId", config.deviceId, sizeof(config.deviceId));
  copyArg("brandName", config.brandName, sizeof(config.brandName));
  copyArg("oledTitle", config.oledTitle, sizeof(config.oledTitle));
  copyArg("deviceToken", config.deviceToken, sizeof(config.deviceToken));
  copyArg("mqttBroker", config.mqttBroker, sizeof(config.mqttBroker));
  copyArg("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  copyArg("mqttPass", config.mqttPass, sizeof(config.mqttPass));
  copyArg("mqttTopicBase", config.mqttTopicBase, sizeof(config.mqttTopicBase));
  copyArg("webhookUrl", config.webhookUrl, sizeof(config.webhookUrl));

  if (server.hasArg("mqttPort")) {
    config.mqttPort = server.arg("mqttPort").toInt();
  }
  if (server.hasArg("defaultConvFactor")) {
    config.defaultConvFactor = server.arg("defaultConvFactor").toFloat();
  }
  if (server.hasArg("valveDebounceMs")) {
    config.valveDebounceMs = server.arg("valveDebounceMs").toInt();
  }

  saveConfig();

  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:0 auto;padding:20px}"
    ".ok{background:#d4edda;color:#155724;padding:20px;border-radius:8px;margin-top:20px;text-align:center}"
    "a{display:inline-block;margin-top:15px;color:#007bff}</style></head><body>"
    "<div class='ok'><h2>Configuração salva com sucesso!</h2>"
    "<p>Reinicie o dispositivo para aplicar alterações de MQTT/broker e o título do display OLED.</p>"
    "<a href='/'>Voltar</a></div></body></html>");
  server.send(200, "text/html", html);
}

void WebServer::handleStatus() {
  if (!checkAuth()) {
    return;
  }
  StaticJsonDocument<256> doc;
  doc["deviceId"] = config.deviceId;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["volumeMl"] = flowMilliLitres;
  doc["valor"] = totalValue;
  doc["chopp"] = descricao.length() > 0 ? descricao : "Chopp";
  doc["codCliente"] = codCliente;
  doc["conversionFactor"] = conversionFactor;

  char buf[256];
  serializeJson(doc, buf, sizeof(buf));
  server.send(200, "application/json", buf);
}

void WebServer::handleRestart() {
  if (!checkAuth()) {
    return;
  }
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:0 auto;padding:20px;text-align:center}"
    ".warn{background:#fff3cd;color:#856404;padding:20px;border-radius:8px;margin-top:20px}</style></head><body>"
    "<div class='warn'><h2>Reiniciando...</h2>"
    "<p>Aguarde alguns segundos e reconecte.</p></div></body></html>");
  server.send(200, "text/html", html);
  delay(500);
  ESP.restart();
}

void WebServer::handleWifiPortal() {
  if (!checkAuth()) {
    return;
  }
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  setWifiPortalPending();
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Portal WiFi</title></head><body style='font-family:Arial,sans-serif;text-align:center;padding:40px'>"
    "<h2>A reiniciar…</h2><p>Ligue-se ao AP <strong>");
  html += FLOWMETER_WIFI_AP_NAME;
  html += F("</strong> e abra <strong>192.168.4.1</strong></p></body></html>");
  server.send(200, "text/html", html);
  delay(500);
  ESP.restart();
}

void WebServer::begin() {
  server.on("/", HTTP_GET, std::bind(&WebServer::handleRoot, this));
  server.on("/config", HTTP_POST, std::bind(&WebServer::handleConfig, this));
  server.on("/status", HTTP_GET, std::bind(&WebServer::handleStatus, this));
  server.on("/restart", HTTP_POST, std::bind(&WebServer::handleRestart, this));
  server.on("/wifi-portal", HTTP_POST, std::bind(&WebServer::handleWifiPortal, this));
  server.begin();
  Serial.println("Web server started");
}

void WebServer::handleClient() {
  server.handleClient();
}
