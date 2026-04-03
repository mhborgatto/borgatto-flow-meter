#ifndef WebServer_h
#define WebServer_h

#include <ESP8266WebServer.h>

class WebServer {
public:
  void begin();
  void handleClient();
private:
  ESP8266WebServer server;
  bool checkAuth();
  void handleRoot();
  void handleConfig();
  void handleStatus();
  void handleRestart();
  void handleWifiPortal();
  String buildConfigPage();
};

#endif
