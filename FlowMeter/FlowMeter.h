#ifndef FlowMeter_h
#define FlowMeter_h

#include <c_types.h>

#include "Display.h"

extern Display display;
extern volatile byte pulseCount;
extern volatile long myPulseCount;
extern double flowMilliLitres;
extern double totalValue;
extern double valorMl;
extern String textHeader;
extern double saldo;
extern double quantidade;
extern double conversionFactor;
extern String descricao;
extern bool servingDisplayFrozen;
extern double frozenServingMl;
extern double frozenServingValue;
extern bool httpReportPending;

class FlowMeter {
public:
  static void ICACHE_RAM_ATTR pulseCounter();
  void calculateFlowV1();
  void resetDisplayState();
};

#endif
