#include "Config.h"

static const char MAGIC[] = "FLW";

static constexpr uint16_t kEepromWifiPortalFlag = 500;
static constexpr uint16_t kEepromConfigSchemaMark = 502;
static const uint8_t kConfigSchemaV2 = 0xC5;
static const uint8_t kWifiPortalMark0 = 'W';
static const uint8_t kWifiPortalMark1 = 'P';

DeviceConfig config;

void resetConfigDefaults() {
  memset(&config, 0, sizeof(config));
  memcpy(config.magic, MAGIC, 4);
  strncpy(config.deviceId, "Device1", sizeof(config.deviceId) - 1);
  strncpy(config.brandName, "FlowMeter", sizeof(config.brandName) - 1);
  strncpy(config.mqttBroker, "broker.hivemq.com", sizeof(config.mqttBroker) - 1);
  config.mqttPort = 1883;
  config.mqttUser[0] = '\0';
  config.mqttPass[0] = '\0';
  strncpy(config.mqttTopicBase, "AccesysFlowMeter/Device1", sizeof(config.mqttTopicBase) - 1);
  strncpy(config.webhookUrl, "", sizeof(config.webhookUrl) - 1);
  config.deviceToken[0] = '\0';
  /* ml por pulso; calibrado: 100 pulsos ≈ 350 ml reais → 3.5 */
  config.defaultConvFactor = 3.5f;
  config.valveDebounceMs = 400;
  config.oledTitle[0] = '\0';
}

void loadConfig() {
  EEPROM.get(0, config);
  if (memcmp(config.magic, MAGIC, 4) != 0) {
    Serial.println("EEPROM: magic inválido, carregando defaults");
    resetConfigDefaults();
    saveConfig();
  } else {
    if (EEPROM.read(kEepromConfigSchemaMark) != kConfigSchemaV2) {
      memset(config.oledTitle, 0, sizeof(config.oledTitle));
      saveConfig();
      Serial.println("EEPROM: oledTitle inicializado (upgrade de schema)");
    }
    Serial.println("EEPROM: configuração carregada");
  }
}

void saveConfig() {
  memcpy(config.magic, MAGIC, 4);
  EEPROM.put(0, config);
  EEPROM.write(kEepromConfigSchemaMark, kConfigSchemaV2);
  EEPROM.commit();
  Serial.println("EEPROM: configuração salva");
}

void setWifiPortalPending() {
  if (kEepromWifiPortalFlag + 2 > 512) {
    return;
  }
  EEPROM.write(kEepromWifiPortalFlag, kWifiPortalMark0);
  EEPROM.write(kEepromWifiPortalFlag + 1, kWifiPortalMark1);
  EEPROM.commit();
}

bool isWifiPortalPending() {
  if (kEepromWifiPortalFlag + 2 > 512) {
    return false;
  }
  return EEPROM.read(kEepromWifiPortalFlag) == kWifiPortalMark0
         && EEPROM.read(kEepromWifiPortalFlag + 1) == kWifiPortalMark1;
}

void clearWifiPortalPending() {
  if (kEepromWifiPortalFlag + 2 > 512) {
    return;
  }
  EEPROM.write(kEepromWifiPortalFlag, 0);
  EEPROM.write(kEepromWifiPortalFlag + 1, 0);
  EEPROM.commit();
}
