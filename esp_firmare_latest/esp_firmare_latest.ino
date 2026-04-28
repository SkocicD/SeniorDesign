#include <Arduino.h>
#include "BLE.h"

extern "C" {
  #include "driver/spi_master.h"
}

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
void configureAds1298() {
  adsCommand(CMD_SDATAC);
  delay(10);

  adsWriteRegister(
    REG_CONFIG1,
    CONFIG1_HIGH_RES_1KSPS | CONFIG1_DAISY_EN | CONFIG1_CLK_EN
  );
  delay(5);

  adsWriteRegister(REG_CONFIG2, CONFIG2_DEFAULT);
  delay(5);

  adsWriteRegister(REG_CONFIG3, CONFIG3_DEFAULT | CONFIG3_PD_REFBUF);
  delay(150);

  for (uint8_t reg = REG_CH1SET; reg <= REG_CH8SET; reg++) {
    adsWriteRegister(reg, CH_GAIN_6X | CH_ELECTRODE_IN);
    delay(1);
  }

  Serial.println("Register readback:");

  Serial.print("ID      = 0x");
  Serial.println(adsReadRegister(REG_ID), HEX);

  Serial.print("CONFIG1 = 0x");
  Serial.println(adsReadRegister(REG_CONFIG1), HEX);

  Serial.print("CONFIG2 = 0x");
  Serial.println(adsReadRegister(REG_CONFIG2), HEX);

  Serial.print("CONFIG3 = 0x");
  Serial.println(adsReadRegister(REG_CONFIG3), HEX);
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
  char voltageString[20];
  uint8_t comma = ',';
  uint8_t newline = '\n';
  ssize_t voltageStringLen;
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

      
      voltageStringLen=snprintf(voltageString, sizeof(voltageString), "%.06f", volts);
      BLE::sendData((uint8_t *)voltageString, voltageStringLen);
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("ESP32-S3 ADS1298 single-file DMA test");

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
