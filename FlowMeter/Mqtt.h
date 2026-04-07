#ifndef Mqtt_h
#define Mqtt_h

#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "Display.h"

extern Display display;
extern char mqttTopicSet[];
extern void IRAM_ATTR pulseCounter();
extern String textHeader;
extern int sensor;
extern volatile byte pulseCount;
extern float totalLitres;
extern unsigned int totalMilliLitres;
extern volatile byte pulseTotal;
extern volatile long myPulseCount;

extern double valorMl;
extern double saldo;
extern String descricao;
extern int comando;
extern int codCliente;
extern double totalValue;
extern double quantidade;
extern double conversionFactor;

extern volatile uint8_t mqttUiPending;

extern volatile bool valveStabilizing;
extern unsigned long valveStabilizeStart;
extern volatile bool enableFlowPulseCounting;

extern bool servingDisplayFrozen;
extern unsigned long tempoTorneira;
extern unsigned long lastFlowActivityMs;

class Mqtt {
public:
  Mqtt();
  bool connect(const char *mqtt_broker, int mqtt_port, const char *mqtt_username, const char *mqtt_password, const char *topic, const char *topicSet);
  void publish(const char *topic, String msg);
  static void callback(char *topic, byte *payload, unsigned int length);
  void loop();
private:
  bool reconnect();
  WiFiClient wifiClient;
  PubSubClient mqttClient;
  char _broker[64];
  uint16_t _port;
  char _user[32];
  char _pass[32];
  char _topic[68];
  char _topicSet[72];
  unsigned long _lastReconnectAttempt;
};

#endif
