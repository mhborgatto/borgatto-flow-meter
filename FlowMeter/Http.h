#ifndef Http_h
#define Http_h

#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

class Http {
public:
  void sendReport();
private:
  char jsonOutput[256];
};

#endif
