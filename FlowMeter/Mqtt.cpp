#include "Mqtt.h"
#include "Config.h"
#include "FlowMeter.h"
#include <cstring>

extern FlowMeter flowMeter;

Mqtt::Mqtt()
  : mqttClient(wifiClient), _port(0), _lastReconnectAttempt(0) {
  _broker[0] = '\0';
  _user[0] = '\0';
  _pass[0] = '\0';
  _topic[0] = '\0';
  _topicSet[0] = '\0';
}

bool Mqtt::connect(const char *mqtt_broker, int mqtt_port, const char *mqtt_username, const char *mqtt_password, const char *topic, const char *topicSet) {
  Serial.println("Inicio Conectando ao broker mqtt");

  strncpy(_broker, mqtt_broker, sizeof(_broker) - 1);  _broker[sizeof(_broker) - 1] = '\0';
  _port = mqtt_port;
  strncpy(_user, mqtt_username, sizeof(_user) - 1);     _user[sizeof(_user) - 1] = '\0';
  strncpy(_pass, mqtt_password, sizeof(_pass) - 1);     _pass[sizeof(_pass) - 1] = '\0';
  strncpy(_topic, topic, sizeof(_topic) - 1);            _topic[sizeof(_topic) - 1] = '\0';
  strncpy(_topicSet, topicSet, sizeof(_topicSet) - 1);   _topicSet[sizeof(_topicSet) - 1] = '\0';

  mqttClient.setServer(_broker, _port);
  mqttClient.setCallback(Mqtt::callback);

  byte tentativa = 0;
  do {
    String client_id = "FlowMeter-";
    client_id += String(WiFi.macAddress());

    if (mqttClient.connect(client_id.c_str(), _user, _pass)) {
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
    mqttClient.publish(_topic, "OK");
    mqttClient.subscribe(_topic);
    mqttClient.publish(_topicSet, "0");
    mqttClient.subscribe(_topicSet);
    _lastReconnectAttempt = 0;
    return 1;
  } else {
    Serial.println("Não conectado ao broker");
    return 0;
  }
}

bool Mqtt::reconnect() {
  String client_id = "FlowMeter-";
  client_id += String(WiFi.macAddress());

  Serial.printf("[MQTT] Reconectando ao broker %s:%u ...\n", _broker, _port);

  if (mqttClient.connect(client_id.c_str(), _user, _pass)) {
    Serial.println("[MQTT] Reconectado com sucesso");
    mqttClient.subscribe(_topic);
    mqttClient.subscribe(_topicSet);
    return true;
  }

  Serial.printf("[MQTT] Falha ao reconectar (state=%d)\n", mqttClient.state());
  return false;
}

void Mqtt::loop() {
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnectAttempt >= 5000) {
      _lastReconnectAttempt = now;
      if (reconnect()) {
        _lastReconnectAttempt = 0;
      }
    }
    return;
  }
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

  tempoTorneira = doc["tempoTorneira"] | 0UL;

  offsetResidualMl = doc["offsetResidualMl"] | 0.0;
  intervaloPulsoMinUs = doc["intervaloPulsoMinUs"] | 0UL;
  modoDesenvolvimento = doc["modoDesenvolvimento"] | false;

  Serial.println();
  Serial.println("-----------------------");
  Serial.printf("[MQTT] comando=%d  valorMl=%.4f  saldo=%.2f  quantidade=%.2f  codCliente=%d  tempoTorneira=%lus  offsetResidualMl=%.2f  intervaloPulsoMinUs=%lu  modoDev=%d\n",
                comando, valorMl, saldo, quantidade, codCliente, tempoTorneira, offsetResidualMl, intervaloPulsoMinUs, modoDesenvolvimento);

  if (comando == 0) {
    Serial.println("[MQTT] Comando 0: desligando válvula e bomba");
    enableFlowPulseCounting = false;
    pumpPreStartActive = false;
    detachInterrupt(digitalPinToInterrupt(sensor));
    servingDisplayFrozen = false;
    digitalWrite(D1, LOW);
    digitalWrite(pumpPin, LOW);
    valveStabilizing = true;
    valveStabilizeStart = millis();
    mqttUiPending = 1;
  } else if (comando == 1) {
    Serial.println("[MQTT] Comando 1: zerando contadores e ativando sensor antes do pré-start");
    enableFlowPulseCounting = true;
    servingDisplayFrozen = false;

    // Zerar contadores e ativar interrupção ANTES de ligar a bomba
    noInterrupts();
    pulseCount = 0;
    myPulseCount = 0;
    interrupts();
    flowMilliLitres = 0;
    totalValue = 0;
    lastPulseUs = 0;
    attachInterrupt(digitalPinToInterrupt(sensor), flowMeter.pulseCounter, FALLING);

    // Agora sim, ligar a bomba
    digitalWrite(pumpPin, HIGH);
    pumpPreStartActive = true;
    pumpPreStartBegin = millis();
    lastFlowActivityMs = millis();
    mqttUiPending = 2;
  } else {
    Serial.printf("[MQTT] Comando %d ignorado (somente 0 e 1 são tratados)\n", comando);
  }
}
