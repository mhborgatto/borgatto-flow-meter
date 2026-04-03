#ifndef FlowMeter_h
#define FlowMeter_h

#include <c_types.h>

#include "Display.h"

extern Display display;
extern volatile byte pulseCount;
extern volatile long myPulseCount;
extern unsigned long flowMilliLitres;
extern double totalValue;
extern double valorMl;
extern String textHeader;
extern double saldo;
extern double quantidade;
extern float conversionFactor;
extern String descricao;
extern bool servingDisplayFrozen;
extern unsigned long frozenServingMl;
extern double frozenServingValue;
extern bool httpReportPending;

class FlowMeter {
public:
  static void ICACHE_RAM_ATTR pulseCounter();
  void calculateFlowV1();
  void resetDisplayState();
};

#endif
