#include <Arduino.h>
#include "BLE.h"
#include "BatteryManager.h"


extern "C" {
  #include "driver/spi_master.h"
}

#define ERROR_LED 16

// =====================================================
// ESP32-S3 -> ADS1298 PIN MAP
// =====================================================
#define ADS_CS      10
#define ADS_MOSI    11
#define ADS_SCLK    12
#define ADS_MISO    13
#define ADS_DRDY    14
#define ADS_START   21
#define ADS_RESET   47
#define ADS_PWDN    46

// =====================================================
// ADS1298 SYSTEM SETTINGS
// =====================================================
#define NUM_ADS               2
#define ADS_CHANNELS          8
#define BYTES_PER_ADS_FRAME   27   // 3 status + 8 channels * 3 bytes
#define TOTAL_FRAME_BYTES     (NUM_ADS * BYTES_PER_ADS_FRAME)

#define ADS_VREF              2.4f
#define ADS_GAIN              6.0f

// =====================================================
// ADS1298 COMMANDS
// =====================================================
#define CMD_WAKEUP   0x02
#define CMD_STANDBY  0x04
#define CMD_RESET    0x06
#define CMD_START    0x08
#define CMD_STOP     0x0A
#define CMD_RDATAC   0x10
#define CMD_SDATAC   0x11
#define CMD_RDATA    0x12
#define CMD_RREG     0x20
#define CMD_WREG     0x40

// =====================================================
// ADS1298 REGISTERS
// =====================================================
#define REG_ID       0x00
#define REG_CONFIG1  0x01
#define REG_CONFIG2  0x02
#define REG_CONFIG3  0x03

#define REG_CH1SET   0x05
#define REG_CH8SET   0x0C

#define REG_LOFF       0x04
#define REG_RLD_SENSP  0x0D
#define REG_RLD_SENSN  0x0E
#define REG_LOFF_SENSP 0x0F
#define REG_LOFF_SENSN 0x10
#define REG_LOFF_FLIP  0x11
#define REG_GPIO       0x14
#define REG_PACE       0x15
#define REG_RESP       0x16
#define REG_CONFIG4    0x17
#define REG_WCT1       0x18
#define REG_WCT2       0x19

// =====================================================
// ADS1298 REGISTER VALUES / BIT MASKS
// =====================================================

// CONFIG1
#define CONFIG1_HR               0x80
#define CONFIG1_DAISY_EN         0x40
#define CONFIG1_CLK_EN           0x20
#define CONFIG1_DR_1KSPS         0x04
#define CONFIG1_HIGH_RES_1KSPS   (CONFIG1_HR | CONFIG1_DR_1KSPS)

// CONFIG2
#define CONFIG2_DEFAULT          0xC0

// CONFIG3
#define CONFIG3_DEFAULT          0x60
#define CONFIG3_PD_REFBUF        0x80

// CHnSET
#define CH_GAIN_6X               0x00
#define CH_ELECTRODE_IN          0x00

// =====================================================
// GLOBAL VARIABLES
// =====================================================
spi_device_handle_t adsSpi;

uint8_t txFrame[TOTAL_FRAME_BYTES];
uint8_t rxFrame[TOTAL_FRAME_BYTES];

volatile bool drdyFlag = false;

BatteryManager batteryManager(6, 7);

// =====================================================
// INTERRUPT
// =====================================================
void IRAM_ATTR drdyISR() {
  drdyFlag = true;
}

// =====================================================
// SPI DMA SETUP
// =====================================================
void setupSpiDma() {
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = ADS_MOSI;
  buscfg.miso_io_num = ADS_MISO;
  buscfg.sclk_io_num = ADS_SCLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = TOTAL_FRAME_BYTES;

  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = 1000000;   // 1 MHz for bring-up
  devcfg.mode = 1;                   // ADS1298 = SPI mode 1
  devcfg.spics_io_num = ADS_CS;
  devcfg.queue_size = 1;

  esp_err_t ret;

  ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    Serial.print("SPI bus init failed: ");
    Serial.println(ret);
    while (1);
  }

  ret = spi_bus_add_device(SPI2_HOST, &devcfg, &adsSpi);
  if (ret != ESP_OK) {
    Serial.print("SPI device add failed: ");
    Serial.println(ret);
    while (1);
  }
}

// =====================================================
// SPI TRANSFER
// =====================================================
bool spiTransfer(uint8_t *tx, uint8_t *rx, size_t len) {
  spi_transaction_t t = {};
  t.length = len * 8;
  t.tx_buffer = tx;
  t.rx_buffer = rx;

  esp_err_t ret = spi_device_transmit(adsSpi, &t);

  if (ret != ESP_OK) {
    Serial.print("SPI transfer failed: ");
    Serial.println(ret);
    return false;
  }

  return true;
}

// =====================================================
// ADS1298 COMMAND FUNCTIONS
// =====================================================
void adsCommand(uint8_t cmd) {
  uint8_t tx[1] = { cmd };
  uint8_t rx[1] = { 0 };

  spiTransfer(tx, rx, 1);
  delayMicroseconds(10);
}

uint8_t adsReadRegister(uint8_t reg) {
  uint8_t tx[3] = {
    uint8_t(CMD_RREG | reg),
    0x00,
    0x00
  };

  uint8_t rx[3] = { 0 };

  spiTransfer(tx, rx, 3);
  delayMicroseconds(10);

  return rx[2];
}

void adsWriteRegister(uint8_t reg, uint8_t value) {
  uint8_t tx[3] = {
    uint8_t(CMD_WREG | reg),
    0x00,
    value
  };

  uint8_t rx[3] = { 0 };

  spiTransfer(tx, rx, 3);
  delayMicroseconds(10);
}

// =====================================================
// ADS1298 HARDWARE RESET
// =====================================================
void adsHardwareReset() {
  digitalWrite(ADS_PWDN, HIGH);
  digitalWrite(ADS_RESET, HIGH);
  digitalWrite(ADS_START, LOW);

  delay(100);

  digitalWrite(ADS_RESET, LOW);
  delay(10);
  digitalWrite(ADS_RESET, HIGH);

  delay(150);
}

// =====================================================
// ADS1298 CONFIGURATION
// =====================================================
struct AdsRegConfig {
  uint8_t reg;
  uint8_t value;
};

const AdsRegConfig adsConfig[] = {
  {REG_CONFIG1,    0xE5},
  {REG_CONFIG2,    0x10},
  {REG_CONFIG3,    0xC8},
  {REG_LOFF,       0x03},

  {REG_CH1SET,     0x80},
  {REG_CH1SET + 1, 0x80},
  {REG_CH1SET + 2, 0x80},
  {REG_CH1SET + 3, 0x80},
  {REG_CH1SET + 4, 0x80},
  {REG_CH1SET + 5, 0x60},
  {REG_CH1SET + 6, 0x60},
  {REG_CH1SET + 7, 0x60},

  {REG_RLD_SENSP,  0x00},
  {REG_RLD_SENSN,  0x00},
  {REG_LOFF_SENSP, 0x00},
  {REG_LOFF_SENSN, 0x00},
  {REG_LOFF_FLIP,  0x00},

  {REG_GPIO,       0x00},
  {REG_PACE,       0x00},
  {REG_RESP,       0xF0},
  {REG_CONFIG4,    0x20},
  {REG_WCT1,       0x0A},
  {REG_WCT2,       0x23},
};

void configureAds1298() {
  adsCommand(CMD_SDATAC);
  delay(10);

  for (size_t i = 0; i < sizeof(adsConfig) / sizeof(adsConfig[0]); i++) {
    adsWriteRegister(adsConfig[i].reg, adsConfig[i].value);
    delayMicroseconds(10);
  }

  Serial.println("ADS1298 register readback:");

  for (size_t i = 0; i < sizeof(adsConfig) / sizeof(adsConfig[0]); i++) {
    uint8_t actual = adsReadRegister(adsConfig[i].reg);

    Serial.print("REG 0x");
    Serial.print(adsConfig[i].reg, HEX);
    Serial.print(" = 0x");
    Serial.print(actual, HEX);

    if (actual != adsConfig[i].value) {
      Serial.print("  EXPECTED 0x");
      Serial.print(adsConfig[i].value, HEX);
      Serial.print("  MISMATCH");
      digitalWrite(ERROR_LED, HIGH);   // Turn on fault LED
    }

    Serial.println();
  }
}

// =====================================================
// DATA HELPERS
// =====================================================
int32_t signExtend24(uint32_t raw) {
  if (raw & 0x800000) {
    raw |= 0xFF000000;
  }

  return (int32_t)raw;
}

float codeToVolts(int32_t code) {
  return ((float)code * ADS_VREF) / (ADS_GAIN * 8388607.0f);
}

// =====================================================
// READ ADS DATA FRAME
// =====================================================
void readDataFrame() {
  memset(txFrame, 0x00, TOTAL_FRAME_BYTES);
  memset(rxFrame, 0x00, TOTAL_FRAME_BYTES);

  spiTransfer(txFrame, rxFrame, TOTAL_FRAME_BYTES);
}

// =====================================================
// PRINT CSV VOLTAGES
// =====================================================
void printVoltagesCsv() {
  for (int dev = 0; dev < NUM_ADS; dev++) {
    int base = dev * BYTES_PER_ADS_FRAME;
    int chBase = base + 3;

    for (int ch = 0; ch < ADS_CHANNELS; ch++) {
      int i = chBase + ch * 3;

      uint32_t raw =
        ((uint32_t)rxFrame[i] << 16) |
        ((uint32_t)rxFrame[i + 1] << 8) |
        ((uint32_t)rxFrame[i + 2]);

      int32_t code = signExtend24(raw);
      float volts = codeToVolts(code);

      Serial.print(volts, 6);

      if (!(dev == NUM_ADS - 1 && ch == ADS_CHANNELS - 1)) {
        Serial.print(",");
      }
    }
  }

  Serial.println();
}


void sendVoltagesBLE() {
  float voltages[16];
  for (int dev = 0; dev < NUM_ADS; dev++) {
    int base = dev * BYTES_PER_ADS_FRAME;
    int chBase = base + 3;

    for (int ch = 0; ch < ADS_CHANNELS; ch++) {
      int i = chBase + ch * 3;

      uint32_t raw =
        ((uint32_t)rxFrame[i] << 16) |
        ((uint32_t)rxFrame[i + 1] << 8) |
        ((uint32_t)rxFrame[i + 2]);

      int32_t code = signExtend24(raw);
      float volts = codeToVolts(code);

      voltages[dev * ADS_CHANNELS + ch] = volts;
    }
  }
  BLE::sendData((uint8_t *)voltages, sizeof(voltages));
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("ESP32-S3 ADS1298 single-file DMA test");

  pinMode(ERROR_LED, OUTPUT);
  digitalWrite(ERROR_LED, LOW);

  Serial.println("Setting up BQ25186...");

  if (batteryManager.begin()) {
    Serial.println("BQ25186 configured.");
    batteryManager.printRegisters();
  } else {
    Serial.println("BQ25186 not responding.");
    digitalWrite(ERROR_LED, HIGH);   // Turn on fault LED
  }

  pinMode(ADS_PWDN, OUTPUT);
  pinMode(ADS_RESET, OUTPUT);
  pinMode(ADS_START, OUTPUT);
  pinMode(ADS_DRDY, INPUT);

  digitalWrite(ADS_PWDN, HIGH);
  digitalWrite(ADS_RESET, HIGH);
  digitalWrite(ADS_START, LOW);

  setupSpiDma();

  adsHardwareReset();

  adsCommand(CMD_SDATAC);
  delay(10);

  uint8_t initialID = adsReadRegister(REG_ID);

  Serial.print("Initial ADS ID = 0x");
  Serial.println(initialID, HEX);

  configureAds1298();

  attachInterrupt(digitalPinToInterrupt(ADS_DRDY), drdyISR, FALLING);

  adsCommand(CMD_RDATAC);
  delay(10);

  digitalWrite(ADS_START, HIGH);

  // Serial.println("Streaming CSV voltages...");
  BLE::setupServer();

}

// =====================================================
// LOOP
// =====================================================
void loop() {
  BLE::checkConnection();
  if (drdyFlag) {
    drdyFlag = false;

    readDataFrame();
    sendVoltagesBLE();
  } 
}
