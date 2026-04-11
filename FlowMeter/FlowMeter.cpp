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

volatile unsigned long lastPulseUs = 0;

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
  if (intervaloPulsoMinUs > 0) {
    unsigned long now = micros();
    if (now - lastPulseUs < intervaloPulsoMinUs) return;
    lastPulseUs = now;
  }
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
      if (modoDesenvolvimento) {
        char d1[22]; snprintf(d1, sizeof(d1), "%s [DONE]", choppName());
        char d2[22]; snprintf(d2, sizeof(d2), "V:%.0fml P:%ld", frozenServingMl, frozenSnapshotPulses);
        char d3[22]; snprintf(d3, sizeof(d3), "R$:%.2f", frozenServingValue);
        char d4[22]; snprintf(d4, sizeof(d4), "F:%.4f", conversionFactor);
        char d5[22]; snprintf(d5, sizeof(d5), "Ofs:%.1f Int:%lu", offsetResidualMl, intervaloPulsoMinUs);
        char d6[22]; snprintf(d6, sizeof(d6), "Cli:%d Sld:%.1f", codCliente, saldo);
        display.showDebugFilling(d1, d2, d3, d4, d5, d6);
      } else {
        display.showFilling(textHeader, "Concluído", msg_vol_out, msg_out);
      }
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

    // Compensa o volume preso entre sensor e solenóide
    const double effectiveQty = (quantidade > 0.0 && offsetResidualMl > 0.0)
        ? quantidade - offsetResidualMl
        : quantidade;
    const double residualValueOffset = (offsetResidualMl > 0.0)
        ? offsetResidualMl * valorMl / 100.0
        : 0.0;
    const double effectiveSaldo = (saldo > 0.0 && residualValueOffset > 0.0)
        ? saldo - residualValueOffset
        : saldo;

    const bool hitSaldo = (effectiveSaldo > 0.0 && valorMl > 1e-9 && totalValue >= effectiveSaldo);
    const bool hitQty = (effectiveQty > 0.0 && flowMilliLitres >= effectiveQty);

    if (hitSaldo || hitQty) {
      digitalWrite(D1, LOW);
      digitalWrite(pumpPin, LOW);

      // Display congela nos valores ORIGINAIS (não efetivos)
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

      Serial.println("=== RELATÓRIO DE SERVIDA ===");
      Serial.printf("[CALIB] Pulsos totais: %ld\n", pc);
      Serial.printf("[CALIB] Volume sensor real: %.1f mL\n", flowMilliLitres);
      Serial.printf("[CALIB] Volume frozen: %.1f mL\n", frozenServingMl);
      Serial.printf("[CALIB] Valor frozen: R$ %.2f\n", frozenServingValue);
      Serial.printf("[CALIB] Fator: %.6f mL/pulso\n", conversionFactor);
      Serial.printf("[CALIB] offsetResidualMl=%.2f  intervaloPulsoMinUs=%lu\n", offsetResidualMl, intervaloPulsoMinUs);
      Serial.printf("[CALIB] Limite por: %s\n", hitSaldo ? "saldo" : "quantidade");
      Serial.println("============================");

      snprintf(msg_vol_out, sizeof(msg_vol_out), "V: %.0f ml  P:%ld", frozenServingMl,
               frozenSnapshotPulses);
      char msg_out[24];
      snprintf(msg_out, sizeof(msg_out), "R$: %.2f", frozenServingValue);
      if (modoDesenvolvimento) {
        char d1[22]; snprintf(d1, sizeof(d1), "%s [DONE]", choppName());
        char d2[22]; snprintf(d2, sizeof(d2), "V:%.0fml P:%ld", frozenServingMl, frozenSnapshotPulses);
        char d3[22]; snprintf(d3, sizeof(d3), "R$:%.2f vMl:%.2f", frozenServingValue, valorMl);
        char d4[22]; snprintf(d4, sizeof(d4), "F:%.4f", conversionFactor);
        char d5[22]; snprintf(d5, sizeof(d5), "Ofs:%.1f Int:%lu", offsetResidualMl, intervaloPulsoMinUs);
        char d6[22]; snprintf(d6, sizeof(d6), "Real:%.0fml %s", flowMilliLitres, hitSaldo ? "$" : "Q");
        display.showDebugFilling(d1, d2, d3, d4, d5, d6);
      } else {
        display.showFilling(textHeader, "Concluído", msg_vol_out, msg_out);
      }
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
      if (modoDesenvolvimento) {
        char d1[22]; snprintf(d1, sizeof(d1), "%s", choppName());
        char d2[22]; snprintf(d2, sizeof(d2), "V:%.0fml P:%ld", flowMilliLitres, pc);
        char d3[22]; snprintf(d3, sizeof(d3), "R$:%.2f vMl:%.2f", totalValue, valorMl);
        char d4[22]; snprintf(d4, sizeof(d4), "F:%.4f S:%.1f", conversionFactor, saldo);
        char d5[22]; snprintf(d5, sizeof(d5), "Qty:%.0f Ofs:%.1f", quantidade, offsetResidualMl);
        char d6[22]; snprintf(d6, sizeof(d6), "Cli:%d T:%lus", codCliente, tempoTorneira);
        display.showDebugFilling(d1, d2, d3, d4, d5, d6);
      } else {
        display.showFilling(textHeader, choppName(), msg_vol_out, msg_out);
      }
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
