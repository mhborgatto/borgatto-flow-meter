#include "FlowMeter.h"
#include <cmath>

namespace {
const unsigned long DISPLAY_INTERVAL_MS = 150;
const unsigned long DISPLAY_MIN_MS = 50;
unsigned long lastDisplayMs = 0;
double lastPaintedMl = -1.0;
double lastPaintedValue = -1.0;
long lastPaintedPulses = -1;
long frozenSnapshotPulses = 0;
long lastDebugLoggedMyPulseCount = 0;
long lastActivityPulseCount = 0;
}  // namespace

static const char* choppName() {
  return (descricao.length() > 0) ? descricao.c_str() : "Chopp";
}

void FlowMeter::resetDisplayState() {
  lastDisplayMs = 0;
  lastPaintedMl = -1.0;
  lastPaintedValue = -1.0;
  lastPaintedPulses = -1;
  lastActivityPulseCount = 0;
}

void ICACHE_RAM_ATTR FlowMeter::pulseCounter() {
  pulseCount++;
  myPulseCount++;
}

void FlowMeter::calculateFlowV1() {
  if (servingDisplayFrozen) {
    char msg_vol_out[40];
    snprintf(msg_vol_out, sizeof(msg_vol_out), "V: %.0f ml  P:%ld", frozenServingMl,
             frozenSnapshotPulses);
    char msg_out[24];
    snprintf(msg_out, sizeof(msg_out), "R$: %.2f", frozenServingValue);

    unsigned long now = millis();
    bool intervalElapsed = (now - lastDisplayMs >= DISPLAY_INTERVAL_MS);
    bool valueChanged =
        (fabs(frozenServingMl - lastPaintedMl) > 0.0005) || (fabs(frozenServingValue - lastPaintedValue) > 0.01);
    bool shouldPaint =
        intervalElapsed || (valueChanged && (now - lastDisplayMs >= DISPLAY_MIN_MS));

    if (shouldPaint) {
      display.showFilling(textHeader, "Concluído", msg_vol_out, msg_out);
      lastDisplayMs = now;
      lastPaintedMl = frozenServingMl;
      lastPaintedValue = frozenServingValue;
    }
    return;
  }

  noInterrupts();
  const long pc = myPulseCount;
  interrupts();

  flowMilliLitres = round(static_cast<double>(pc) * conversionFactor * 1000.0) / 1000.0;

  if (pc > 0 && pc != lastActivityPulseCount) {
    lastFlowActivityMs = millis();
    lastActivityPulseCount = pc;
  }

  if (pc != lastDebugLoggedMyPulseCount) {
    lastDebugLoggedMyPulseCount = pc;
    const double mlBruto = static_cast<double>(pc) * static_cast<double>(conversionFactor);
    Serial.print("[pulso] count=");
    Serial.print(pc);
    Serial.print("\tmlPorPulso=");
    Serial.print(conversionFactor, 6);
    Serial.print("\tmlRound=");
    Serial.print(flowMilliLitres, 3);
    Serial.print("\tmlCalc=");
    Serial.println(mlBruto, 4);
  }

  char msg_vol_out[40];
  snprintf(msg_vol_out, sizeof(msg_vol_out), "V: %.0f ml  P:%ld", flowMilliLitres, pc);

  if (flowMilliLitres > 0.0005) {
    totalValue = flowMilliLitres * valorMl / 100.0;

    const bool hitSaldo = (saldo > 0.0 && valorMl > 1e-9 && totalValue >= saldo);
    const bool hitQty = (quantidade > 0.0 && flowMilliLitres >= quantidade);

    if (hitSaldo || hitQty) {
      digitalWrite(D1, LOW);
      digitalWrite(pumpPin, LOW);

      if (hitSaldo) {
        frozenServingValue = saldo;
        frozenServingMl = round(saldo * 100.0 / valorMl * 1000.0) / 1000.0;
      } else {
        frozenServingMl = round(quantidade * 1000.0) / 1000.0;
        frozenServingValue = quantidade * valorMl / 100.0;
      }
      frozenSnapshotPulses = pc;
      servingDisplayFrozen = true;
      httpReportPending = true;

      snprintf(msg_vol_out, sizeof(msg_vol_out), "V: %.0f ml  P:%ld", frozenServingMl,
               frozenSnapshotPulses);
      char msg_out[24];
      snprintf(msg_out, sizeof(msg_out), "R$: %.2f", frozenServingValue);
      display.showFilling(textHeader, "Concluído", msg_vol_out, msg_out);
      lastDisplayMs = millis();
      lastPaintedMl = frozenServingMl;
      lastPaintedValue = frozenServingValue;
      lastPaintedPulses = frozenSnapshotPulses;
      return;
    }

    Serial.print("FlowMilliLitres: ");
    Serial.print(flowMilliLitres, 3);
    Serial.print("\t");
    Serial.print("ValorMl: ");
    Serial.print(valorMl);
    Serial.print("\t");
    Serial.print("Value: ");
    Serial.print(totalValue);
    Serial.print("\t");
    Serial.print("Pulsos: ");
    Serial.println(pc);

    unsigned long now = millis();
    bool intervalElapsed = (now - lastDisplayMs >= DISPLAY_INTERVAL_MS);
    bool valueChanged = (fabs(flowMilliLitres - lastPaintedMl) > 0.0005) ||
                        (fabs(totalValue - lastPaintedValue) > 0.01) ||
                        (pc != lastPaintedPulses);
    bool shouldPaint =
        intervalElapsed || (valueChanged && (now - lastDisplayMs >= DISPLAY_MIN_MS));

    if (shouldPaint) {
      char msg_out[24];
      snprintf(msg_out, sizeof(msg_out), "R$: %.2f", totalValue);
      display.showFilling(textHeader, choppName(), msg_vol_out, msg_out);
      lastDisplayMs = now;
      lastPaintedMl = flowMilliLitres;
      lastPaintedValue = totalValue;
      lastPaintedPulses = pc;
    }
  } else if (pc > 0) {
    unsigned long now = millis();
    bool intervalElapsed = (now - lastDisplayMs >= DISPLAY_INTERVAL_MS);
    bool valueChanged = (pc != lastPaintedPulses);
    bool shouldPaint =
        intervalElapsed || (valueChanged && (now - lastDisplayMs >= DISPLAY_MIN_MS));

    if (shouldPaint) {
      display.showFilling(textHeader, choppName(), msg_vol_out, "R$: 0.00");
      lastDisplayMs = now;
      lastPaintedMl = 0;
      lastPaintedValue = 0.0;
      lastPaintedPulses = pc;
    }
  }
}
