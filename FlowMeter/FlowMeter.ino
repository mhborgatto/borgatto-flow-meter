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

unsigned long flowMilliLitres;
unsigned int totalMilliLitres;

float flowLitres;
float totalLitres;
float flowRate;
float calibrationFactor = 120;
float requiredVolume = 100;
float conversionFactor = 3.5f;

double valorMl;
double saldo;
String descricao;
int comando;
int codCliente;
double totalValue;
double quantidade;

volatile uint8_t mqttUiPending = 0;

bool servingDisplayFrozen = false;
unsigned long frozenServingMl = 0;
double frozenServingValue = 0.0;

volatile bool valveStabilizing = false;
unsigned long valveStabilizeStart = 0;

bool httpReportPending = false;

static String choppLabel() {
  return descricao.length() > 0 ? descricao : String("Chopp");
}

void setup() {
  pinMode(D1, OUTPUT);
  digitalWrite(D1, HIGH);
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

  attachInterrupt(digitalPinToInterrupt(sensor), flowMeter.pulseCounter, FALLING);

  display.showFilling(textHeader, choppLabel(), "Aguardando", "Liberação");
}

void loop() {
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
      pulseCount = 0;
      myPulseCount = 0;
      flowMilliLitres = 0;
      totalMilliLitres = 0;
      totalLitres = 0;
      pulseTotal = 0;
      totalValue = 0;
      flowRate = 0.0;
      flowMeter.resetDisplayState();
      attachInterrupt(digitalPinToInterrupt(sensor), flowMeter.pulseCounter, FALLING);
      valveStabilizing = false;
    }
    return;
  }

  flowMeter.calculateFlowV1();
}
