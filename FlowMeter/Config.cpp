#include "Config.h"

static const char MAGIC[] = "FLW";

static constexpr uint16_t kEepromWifiPortalFlag = 500;
static constexpr uint16_t kEepromConfigSchemaMark = 502;
static const uint8_t kConfigSchemaV2 = 0xC5;
static const uint8_t kConfigSchemaV3 = 0xC6;
static const uint8_t kWifiPortalMark0 = 'W';
static const uint8_t kWifiPortalMark1 = 'P';

struct DeviceConfigV2 {
  char     magic[4];
  char     deviceId[32];
  char     brandName[32];
  char     mqttBroker[64];
  uint16_t mqttPort;
  char     mqttUser[32];
  char     mqttPass[32];
  char     mqttTopicBase[64];
  char     webhookUrl[128];
  char     deviceToken[64];
  float    defaultConvFactor;
  uint16_t valveDebounceMs;
  char     oledTitle[32];
};

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
  config.defaultConvFactor = 3.5;
  config.valveDebounceMs = 400;
  config.oledTitle[0] = '\0';
}

void loadConfig() {
  uint8_t schemaVer = EEPROM.read(kEepromConfigSchemaMark);

  if (schemaVer == kConfigSchemaV3) {
    EEPROM.get(0, config);
    if (memcmp(config.magic, MAGIC, 4) != 0) {
      Serial.println("EEPROM: magic inválido, carregando defaults");
      resetConfigDefaults();
      saveConfig();
    } else {
      Serial.println("EEPROM: configuração V3 carregada");
    }
    return;
  }

  DeviceConfigV2 oldCfg;
  EEPROM.get(0, oldCfg);

  if (memcmp(oldCfg.magic, MAGIC, 4) != 0) {
    Serial.println("EEPROM: magic inválido, carregando defaults");
    resetConfigDefaults();
    saveConfig();
    return;
  }

  if (schemaVer != kConfigSchemaV2) {
    memset(oldCfg.oledTitle, 0, sizeof(oldCfg.oledTitle));
    Serial.println("EEPROM: oledTitle inicializado (upgrade pre-V2)");
  }

  memcpy(config.magic, oldCfg.magic, 4);
  memcpy(config.deviceId, oldCfg.deviceId, sizeof(config.deviceId));
  memcpy(config.brandName, oldCfg.brandName, sizeof(config.brandName));
  memcpy(config.mqttBroker, oldCfg.mqttBroker, sizeof(config.mqttBroker));
  config.mqttPort = oldCfg.mqttPort;
  memcpy(config.mqttUser, oldCfg.mqttUser, sizeof(config.mqttUser));
  memcpy(config.mqttPass, oldCfg.mqttPass, sizeof(config.mqttPass));
  memcpy(config.mqttTopicBase, oldCfg.mqttTopicBase, sizeof(config.mqttTopicBase));
  memcpy(config.webhookUrl, oldCfg.webhookUrl, sizeof(config.webhookUrl));
  memcpy(config.deviceToken, oldCfg.deviceToken, sizeof(config.deviceToken));
  config.defaultConvFactor = static_cast<double>(oldCfg.defaultConvFactor);
  config.valveDebounceMs = oldCfg.valveDebounceMs;
  memcpy(config.oledTitle, oldCfg.oledTitle, sizeof(config.oledTitle));

  saveConfig();
  Serial.println("EEPROM: migração V2→V3 concluída (defaultConvFactor float→double)");
}

void saveConfig() {
  memcpy(config.magic, MAGIC, 4);
  EEPROM.put(0, config);
  EEPROM.write(kEepromConfigSchemaMark, kConfigSchemaV3);
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
