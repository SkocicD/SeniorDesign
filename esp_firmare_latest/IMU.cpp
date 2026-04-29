#include <Wire.h>
#include <Arduino.h>
#include <Arduino_BMI270_BMM150.h>


// Stream rate control
#define SAMPLE_PERIOD_MS 10   // 100 Hz serial output

static unsigned long lastSample;

namespace NICE_IMU {
    void init() {
      if (!IMU.begin()) {
        Serial.println("BMI270 failed to initialize.");
      }
      Serial.println("BMI270 initialized.");
    }

    bool readData(float *pf_IMUData, ssize_t IMUDataSize) {
        if (millis() - lastSample < SAMPLE_PERIOD_MS) return false;
        if (IMUDataSize != 6*sizeof(float)) {
            return false;
        }
        bool receivedData = false;
        lastSample = millis();
        if (IMU.accelerationAvailable()) {
            IMU.readAcceleration(pf_IMUData[0], pf_IMUData[1], pf_IMUData[2]);
            receivedData = true;
        }
        if (IMU.gyroscopeAvailable()) {
            IMU.readGyroscope(pf_IMUData[3], pf_IMUData[4], pf_IMUData[5]);
            receivedData = true;
        }
        return receivedData;
    }
}
