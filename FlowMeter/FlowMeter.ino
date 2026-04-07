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

volatile bool valveStabilizing = false;
unsigned long valveStabilizeStart = 0;

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

  if (valveStabilizing) {
    if (millis() - valveStabilizeStart >= (unsigned long)config.valveDebounceMs) {
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
      if (enableFlowPulseCounting) {
        attachInterrupt(digitalPinToInterrupt(sensor), flowMeter.pulseCounter, FALLING);
        lastFlowActivityMs = millis();
      } else {
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
    detachInterrupt(digitalPinToInterrupt(sensor));
    enableFlowPulseCounting = false;

    if (flowMilliLitres > 0.0005) {
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
