#pragma once

#include <Arduino.h>

// One row of sensor values for the CSV stream.
struct SensorValues {
  bool ok = false;
  float tempC = 0.0f;
  float rh = 0.0f;
  float pressurePa = 0.0f;
  uint32_t alsRaw = 0;
  uint32_t uvRaw = 0;
  uint16_t spectral[12] = {};
  // AS7341 exposure parameters so the host can normalise raw ADC values.
  uint8_t as7341Atime = 0;
  uint16_t as7341Astep = 0;
  float as7341Gain = 0.0f;
  int32_t vocIndex = 0;
  int32_t noxIndex = 0;
  uint16_t srawVoc = 0;
  uint16_t srawNox = 0;
  uint16_t co2Ppm = 0;
  // AS7341 闪烁检测结果：0=无闪烁(直流)，1=有闪烁但频率未知，100/120=Hz。
  uint16_t flickerHz = 0;
};

enum class SensorReadStatus : uint8_t {
  Error = 0,
  Pending = 1,  // healthy continuous sensor, but no new sample is ready yet
  Fresh = 2
};

// Enable the independent X3 accessory supply before TFT initialization.
bool powerAccessorySetup();

// Power on all six branches, preserving independent accessory power.
bool powerAllOn();

// Reads one channel (0..5). With powerAlreadyOn=true the caller keeps all
// branches powered and only the mux is switched; with false the channel is
// powered/read/powered-down like RUNALL.
bool readSensor(uint8_t channel, SensorValues& out,
                bool powerAlreadyOn = false);

// Scheduler-facing read that distinguishes an ordinary "not ready yet" from
// an I2C/measurement error. This prevents SCD40's 5 s conversion time from
// being reported as a failed sample during 1 s polls.
SensorReadStatus readSensorStatus(uint8_t channel, SensorValues& out,
                                  bool powerAlreadyOn = false);

// Reset volatile continuous-sensor and algorithm state after a power cycle.
void resetSensorRuntime();
// Reset only the restarted channel; preserve other channels' algorithms.
void resetSensorRuntime(uint8_t channel);

// Change one rail while preserving all other expander output bits. Disconnect
// the mux first to avoid feeding an unpowered sensor through SDA/SCL.
bool setSensorPower(uint8_t channel, bool on);

// Runs a full six-channel sweep once (for RUNALL).
void runAllSensorsOnce();

// Latest SHT41 reading used for SGP41 compensation.
void setSht41Compensation(float tempC, float rh);
void getSht41Compensation(float& tempC, float& rh);

// I2C helpers shared by the rest of the firmware.
bool i2cWriteRegister(uint8_t address, uint8_t reg, uint8_t value);
bool i2cReadRegister(uint8_t address, uint8_t reg, uint8_t& value);
bool i2cReadRegisters(uint8_t address, uint8_t reg, uint8_t* buf, size_t len);
uint8_t i2cProbeAddress(uint8_t address);
bool muxWrite(uint8_t value);
bool muxRead(uint8_t& value);
bool powerAllOff();  // Sensor rails off; accessory X3 stays powered.
bool powerBranchOn(uint8_t channel);
bool muxConnectChannel(uint8_t channel);
bool muxDisconnect();
