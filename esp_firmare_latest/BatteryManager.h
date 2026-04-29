#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include <Arduino.h>
#include <Wire.h>

class BatteryManager {
public:
  BatteryManager(uint8_t sdaPin, uint8_t sclPin);

  bool begin();
  void printStatus();
  void printRegisters();

private:
  uint8_t _sdaPin;
  uint8_t _sclPin;

  bool readReg(uint8_t reg, uint8_t &value);
  bool writeReg(uint8_t reg, uint8_t value);
  bool configureStandard1200mAh();
};

#endif