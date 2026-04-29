#include "BatteryManager.h"

#define BQ25186_ADDR 0x6A

#define REG_STAT0        0x00
#define REG_STAT1        0x01
#define REG_FLAG0        0x02
#define REG_VBAT_CTRL    0x03
#define REG_ICHG_CTRL    0x04
#define REG_CHARGECTRL0  0x05
#define REG_CHARGECTRL1  0x06
#define REG_IC_CTRL      0x07
#define REG_TMR_ILIM     0x08
#define REG_SHIP_RST     0x09

BatteryManager::BatteryManager(uint8_t sdaPin, uint8_t sclPin)
  : _sdaPin(sdaPin), _sclPin(sclPin) {}

bool BatteryManager::begin() {
  Wire.begin(_sdaPin, _sclPin);
  Wire.setClock(100000);

  uint8_t test;
  if (!readReg(REG_IC_CTRL, test)) {
    return false;
  }

  return configureStandard1200mAh();
}

bool BatteryManager::configureStandard1200mAh() {
  bool ok = true;

  // 4.20 V charge voltage
  ok &= writeReg(REG_VBAT_CTRL, 0x46);

  // 240 mA charge current, 0.2C for 1200 mAh battery
  ok &= writeReg(REG_ICHG_CTRL, 0x33);

  // 5% termination current, about 12 mA
  ok &= writeReg(REG_CHARGECTRL0, 0x14);

  // Conservative discharge/UVLO related settings
  ok &= writeReg(REG_CHARGECTRL1, 0x56);

  // TS enabled, 3.0 V low battery threshold, safety timer enabled
  ok &= writeReg(REG_IC_CTRL, 0x97);

  // Input current limit/timer settings, Disable I2C watchdog
  ok &= writeReg(REG_TMR_ILIM, 0x4F);

  // Normal operation, do not enter ship mode
  ok &= writeReg(REG_SHIP_RST, 0x11);



  return ok;
}

bool BatteryManager::readReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(BQ25186_ADDR);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  Wire.requestFrom(BQ25186_ADDR, 1);

  if (Wire.available() < 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool BatteryManager::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BQ25186_ADDR);
  Wire.write(reg);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}

void BatteryManager::printStatus() {
  uint8_t stat0, stat1, flag0;

  if (readReg(REG_STAT0, stat0)) {
    Serial.print("BQ STAT0: 0x");
    Serial.print(stat0, HEX);
    Serial.print("  ");
  }

  if (readReg(REG_STAT1, stat1)) {
    Serial.print("STAT1: 0x");
    Serial.print(stat1, HEX);
    Serial.print("  ");
  }

  if (readReg(REG_FLAG0, flag0)) {
    Serial.print("FLAG0: 0x");
    Serial.print(flag0, HEX);
  }

  Serial.println();
}

void BatteryManager::printRegisters() {
  Serial.println("BQ25186 Registers:");

  for (uint8_t reg = 0x00; reg <= 0x09; reg++) {
    uint8_t value;

    Serial.print("0x");
    Serial.print(reg, HEX);
    Serial.print(" = ");

    if (readReg(reg, value)) {
      Serial.print("0x");
      if (value < 0x10) Serial.print("0");
      Serial.println(value, HEX);
    } else {
      Serial.println("READ FAIL");
    }
  }
}