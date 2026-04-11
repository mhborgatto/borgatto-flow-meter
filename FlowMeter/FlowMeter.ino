#include "Config.h"
#include "Display.h"
#include "Mqtt.h"
#include "Http.h"
#include "WebServer.h"
#include "FlowMeter.h"

#include <WiFiManager.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

Display display;
Mqtt mqtt;
Http http;
WebServer webserver;
FlowMeter flowMeter;

String textHeader;
int sensor = D2;
int pumpPin = D6;

/** Tempo (ms) que a bomba fica ligada antes de abrir a válvula solenóide. */
#define PUMP_PRE_START_MS 200

char mqttTopic[68];
char mqttTopicSet[72];

bool mqttStatus = 0;

volatile byte pulseCount;
volatile byte pulseTotal;
volatile byte previousPulseCount = 0;
volatile long myPulseCount;

long currentMillis = 0;
long previousMillis = 0;
long previousMillisRequest = 0;

int interval = 1000;
int intervalStop = 10000;

double flowMilliLitres;
unsigned int totalMilliLitres;

float flowLitres;
float totalLitres;
float flowRate;
float calibrationFactor = 120;
float requiredVolume = 100;
double conversionFactor = 3.5;

double valorMl;
double saldo;
String descricao;
int comando;
int codCliente;
double totalValue;
double quantidade;

volatile uint8_t mqttUiPending = 0;

bool servingDisplayFrozen = false;
double frozenServingMl = 0.0;
double frozenServingValue = 0.0;

/** Volume (mL) no trecho sensor→solenóide. Recebido via MQTT. */
double offsetResidualMl = 0.0;

/** Intervalo mínimo entre pulsos (µs). 0 = filtro desabilitado. Recebido via MQTT. */
unsigned long intervaloPulsoMinUs = 0;

/** Modo desenvolvimento: exibe dados de debug detalhados no OLED. Recebido via MQTT. */
bool modoDesenvolvimento = false;

/** Modo calibração: ignora limites, conta pulsos e reporta ao final. Recebido via MQTT. */
bool modoCalibracao = false;
unsigned long calibrationStartMs = 0;

volatile bool valveStabilizing = false;
unsigned long valveStabilizeStart = 0;

volatile bool pumpPreStartActive = false;
unsigned long pumpPreStartBegin = 0;

/** Só contabiliza pulsos do medidor após comando MQTT 1 (válvula liberada). */
volatile bool enableFlowPulseCounting = false;

bool httpReportPending = false;

unsigned long tempoTorneira = 0;
unsigned long lastFlowActivityMs = 0;

static String choppLabel() {
  return descricao.length() > 0 ? descricao : String("Chopp");
}

void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, LOW);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);
  pinMode(sensor, INPUT_PULLUP);

  EEPROM.begin(512);
  delay(10);

  Serial.begin(115200);
  Serial.print("Motivo do último reset: ");
  Serial.println(ESP.getResetReason());

  loadConfig();

  textHeader = (config.oledTitle[0] != '\0') ? String(config.oledTitle) : String(config.brandName);
  conversionFactor = config.defaultConvFactor;

  snprintf(mqttTopic, sizeof(mqttTopic), "%s", config.mqttTopicBase);
  snprintf(mqttTopicSet, sizeof(mqttTopicSet), "%s/set", config.mqttTopicBase);

  display.begin();

  display.showWelcome(textHeader, "Inicializando", "Aguarde...", "");

  display.showWelcome(textHeader, "Configurando", "Wifi", "Aguarde...");

  if (isWifiPortalPending()) {
    display.showWelcome(textHeader, "Portal WiFi", String("AP: ") + FLOWMETER_WIFI_AP_NAME, "192.168.4.1");
    WiFiManager wm;
    wm.setConfigPortalTimeout(300);
    wm.startConfigPortal(FLOWMETER_WIFI_AP_NAME, FLOWMETER_WIFI_AP_PASSWORD);
    clearWifiPortalPending();
    ESP.restart();
  }

  WiFiManager wifiManager;
  wifiManager.autoConnect(FLOWMETER_WIFI_AP_NAME, FLOWMETER_WIFI_AP_PASSWORD);

  Serial.print("Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  String ssid = WiFi.SSID();
  String localIp = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];

  display.showWelcome(textHeader, "Wifi Conectado", ssid, localIp);

  Serial.println(WiFi.localIP());
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Gateway: ");
  Serial.print(WiFi.gatewayIP());
  Serial.print("  Máscara: ");
  Serial.println(WiFi.subnetMask());

  display.showWelcome(textHeader, "Configurando", "MQTT", "Aguarde...");

  mqttStatus = mqtt.connect(config.mqttBroker, config.mqttPort, config.mqttUser, config.mqttPass, mqttTopic, mqttTopicSet);

  display.showWelcome(textHeader, "MQTT Conectado", String(config.mqttBroker), String(config.mqttPort));

  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;
  previousMillisRequest = 0;
  myPulseCount = 0;

  webserver.begin();

  detachInterrupt(digitalPinToInterrupt(sensor));

  display.showFilling(textHeader, choppLabel(), "Aguardando", "Liberação");
}

static long safeReadPulseCount() {
  noInterrupts();
  long val = myPulseCount;
  interrupts();
  return val;
}

void loop() {
  yield();

  display.healthCheck();

  if (display.needsRepaint) {
    display.needsRepaint = false;
    flowMeter.resetDisplayState();
    if (servingDisplayFrozen) {
      mqttUiPending = 0;
    } else if (enableFlowPulseCounting) {
      mqttUiPending = 2;
    } else {
      mqttUiPending = 1;
    }
  }

  static unsigned long lastHeapLog = 0;
  if (millis() - lastHeapLog > 30000) {
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[HEAP] Free: %u bytes\n", freeHeap);
    if (freeHeap < 4096) {
      Serial.println("[HEAP] CRITICO: memoria muito baixa, reiniciando...");
      delay(100);
      ESP.restart();
    }
    lastHeapLog = millis();
  }

  if (mqttStatus) {
    mqtt.loop();
  }

  if (mqttUiPending == 1) {
    display.showFilling(textHeader, choppLabel(), "Aguardando", "Liberação");
    mqttUiPending = 0;
  } else if (mqttUiPending == 2) {
    display.showFilling(textHeader, choppLabel(), "Liberado", "Sirva-se");
    mqttUiPending = 0;
  }

  webserver.handleClient();

  currentMillis = millis();

  if (httpReportPending) {
    http.sendReport();
    httpReportPending = false;
  }

  if (pumpPreStartActive) {
    if (millis() - pumpPreStartBegin >= PUMP_PRE_START_MS) {
      digitalWrite(D1, HIGH);
      valveStabilizing = true;
      valveStabilizeStart = millis();
      pumpPreStartActive = false;
      Serial.println("[PUMP] Pre-start concluído, válvula aberta");
    }
    return;
  }

  if (valveStabilizing) {
    if (millis() - valveStabilizeStart >= (unsigned long)config.valveDebounceMs) {
      if (enableFlowPulseCounting) {
        // Interrupção já está ativa desde o comando MQTT 1.
        // Não zerar contadores — fluxo já está sendo contabilizado.
        lastFlowActivityMs = millis();
        Serial.printf("[DEBOUNCE] Estabilização concluída. Pulsos acumulados: %ld\n", myPulseCount);
      } else {
        // Comando 0 (fechar): zerar tudo normalmente
        noInterrupts();
        pulseCount = 0;
        myPulseCount = 0;
        interrupts();
        flowMilliLitres = 0;
        totalMilliLitres = 0;
        totalLitres = 0;
        pulseTotal = 0;
        totalValue = 0;
        flowRate = 0.0;
        flowMeter.resetDisplayState();
        detachInterrupt(digitalPinToInterrupt(sensor));
      }
      valveStabilizing = false;
    }
    return;
  }

  if (enableFlowPulseCounting && tempoTorneira > 0 &&
      millis() - lastFlowActivityMs >= tempoTorneira * 1000UL) {
    Serial.printf("[TIMEOUT] tempoTorneira=%lus expirou. flowMl=%.3f\n",
                  tempoTorneira, flowMilliLitres);
    digitalWrite(D1, LOW);
    digitalWrite(pumpPin, LOW);
    detachInterrupt(digitalPinToInterrupt(sensor));
    enableFlowPulseCounting = false;

    if (modoCalibracao) {
      // Modo calibração: reportar pulsos e duração
      noInterrupts();
      long calibPulses = myPulseCount;
      interrupts();
      unsigned long duracaoMs = millis() - calibrationStartMs;
      double calibMl = static_cast<double>(calibPulses) * conversionFactor;

      Serial.println("=== RELATORIO CALIBRACAO (timeout) ===");
      Serial.printf("[CALIB] Pulsos: %ld\n", calibPulses);
      Serial.printf("[CALIB] Duracao: %lu ms\n", duracaoMs);
      Serial.printf("[CALIB] Fator atual: %.6f\n", conversionFactor);
      Serial.printf("[CALIB] Volume calculado: %.1f mL\n", calibMl);
      Serial.println("Formula: novoFator = volumeRealMl / pulsos");
      Serial.println("============================");

      // Publicar relatório via MQTT
      mqtt.publish(mqttTopic, String("{\"tipo\":\"calibracao\",\"deviceId\":\"")
        + config.deviceId + "\",\"pulsos\":" + calibPulses
        + ",\"duracaoMs\":" + duracaoMs
        + ",\"fatorAtual\":" + String(conversionFactor, 6)
        + ",\"volumeCalculadoMl\":" + String(calibMl, 1) + "}");

      // Mostrar no display
      char d1[22]; snprintf(d1, sizeof(d1), "== CALIBRACAO ==");
      char d2[22]; snprintf(d2, sizeof(d2), "Pulsos: %ld", calibPulses);
      char d3[22]; snprintf(d3, sizeof(d3), "Dur: %.1fs", duracaoMs / 1000.0);
      char d4[22]; snprintf(d4, sizeof(d4), "Fator: %.4f", conversionFactor);
      char d5[22]; snprintf(d5, sizeof(d5), "Calc: %.0f ml", calibMl);
      char d6[22]; snprintf(d6, sizeof(d6), "Medir vol real!");
      display.showDebugFilling(d1, d2, d3, d4, d5, d6);

      modoCalibracao = false;
      mqttUiPending = 0;
    } else if (flowMilliLitres > 0.0005) {
      totalValue = flowMilliLitres * valorMl / 100.0;
      frozenServingMl = flowMilliLitres;
      frozenServingValue = totalValue;
      servingDisplayFrozen = true;
      httpReportPending = true;
    } else {
      mqttUiPending = 1;
    }
    return;
  }

  flowMeter.calculateFlowV1();
}
