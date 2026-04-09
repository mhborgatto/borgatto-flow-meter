#ifndef Config_h
#define Config_h

#include <Arduino.h>
#include <EEPROM.h>

/** SSID do AP de configuração WiFi (WiFiManager). */
#define FLOWMETER_WIFI_AP_NAME     "BorgattoTapMeter"
#define FLOWMETER_WIFI_AP_PASSWORD "12345678"

struct DeviceConfig {
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
  double   defaultConvFactor;
  uint16_t valveDebounceMs;
  /** Linha superior do OLED; se vazio, usa-se `brandName`. */
  char     oledTitle[32];
};

extern DeviceConfig config;

void loadConfig();
void saveConfig();
void resetConfigDefaults();

void setWifiPortalPending();
bool isWifiPortalPending();
void clearWifiPortalPending();

#endif
