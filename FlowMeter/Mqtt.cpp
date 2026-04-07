#include "Mqtt.h"
#include "Config.h"
#include "FlowMeter.h"
#include <cstring>

extern FlowMeter flowMeter;

Mqtt::Mqtt()
  : mqttClient(wifiClient) {}

bool Mqtt::connect(const char *mqtt_broker, int mqtt_port, const char *mqtt_username, const char *mqtt_password, const char *topic, const char *topicSet) {
  Serial.println("Inicio Conectando ao broker mqtt");
  byte tentativa = 0;
  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(Mqtt::callback);

  do {
    String client_id = "FlowMeter-";
    client_id += String(WiFi.macAddress());

    if (mqttClient.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Exito na conexão:");
      Serial.printf("Cliente %s conectado ao broker\n", client_id.c_str());
    } else {
      Serial.print("Falha ao conectar: ");
      Serial.print(mqttClient.state());
      Serial.println();
      Serial.print("Tentativa: ");
      Serial.println(tentativa);
      delay(2000);
    }
    tentativa++;
  } while (!mqttClient.connected() && tentativa < 5);

  if (tentativa < 5) {
    mqttClient.publish(topic, "OK");
    mqttClient.subscribe(topic);
    mqttClient.publish(topicSet, "0");
    mqttClient.subscribe(topicSet);
    return 1;
  } else {
    Serial.println("Não conectado ao broker");
    return 0;
  }
}

void Mqtt::loop() {
  mqttClient.loop();
}

void Mqtt::publish(const char *topic, String msg) {
  mqttClient.publish(topic, msg.c_str());
}

void Mqtt::callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  if (length == 0 || length > 250) {
    Serial.println();
    Serial.println("MQTT: payload com tamanho inválido");
    return;
  }

  static char buf[251];
  memcpy(buf, payload, length);
  buf[length] = '\0';

  static StaticJsonDocument<256> doc;
  doc.clear();
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    Serial.println();
    Serial.print("JSON parse fail: ");
    Serial.println(err.c_str());
    return;
  }

  if (config.deviceToken[0] != '\0') {
    const char *receivedToken = doc["token"] | "";
    if (strcmp(receivedToken, config.deviceToken) != 0) {
      Serial.println();
      Serial.println("MQTT: token inválido, mensagem ignorada");
      return;
    }
  }

  valorMl = doc["valorMl"];
  saldo = doc["saldo"];
  {
    JsonVariant v = doc["descricao"];
    descricao = v.isNull() ? String() : String(v.as<const char *>());
  }
  comando = doc["comando"];
  codCliente = doc["codCliente"];
  quantidade = doc["quantidade"];

  double recvConv = doc["fatorConversao"] | 0.0;
  if (recvConv > 1e-6) {
    conversionFactor = recvConv;
  }

  Serial.println();
  Serial.println("-----------------------");

  if (comando == 0) {
    Serial.println("Desacione o Pino");
    enableFlowPulseCounting = false;
    detachInterrupt(digitalPinToInterrupt(sensor));
    servingDisplayFrozen = false;
    digitalWrite(D1, LOW);
    valveStabilizing = true;
    valveStabilizeStart = millis();
    mqttUiPending = 1;
  }

  if (comando == 1) {
    Serial.println("Acione o Pino");
    enableFlowPulseCounting = true;
    detachInterrupt(digitalPinToInterrupt(sensor));
    servingDisplayFrozen = false;
    digitalWrite(D1, HIGH);
    valveStabilizing = true;
    valveStabilizeStart = millis();
    mqttUiPending = 2;
  }
}
