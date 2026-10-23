#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "force_math.h"
#include "fft_pins.h"

// This stand-alone bench build belongs to the proposed FFT tactile block.
// It does not initialize the LCD or operate the FFS branch.
namespace {
constexpr uint8_t kAddress = 0x48;
constexpr int kSda = fft_pins::kI2cSda, kScl = fft_pins::kI2cScl;
Preferences preferences;
fft_force::Calibration calibration;
String command;
int16_t supplyRaw = 0;
uint32_t lastSupplyMs = 0, lastMissingMs = 0;
bool supplyValid = false, supplyFresh = false;

bool writeRegister(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg); Wire.write(uint8_t(value >> 8)); Wire.write(uint8_t(value));
  return Wire.endTransmission() == 0;
}
bool readRegister(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(kAddress); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddress, uint8_t(2)) != 2) return false;
  value = (uint16_t(Wire.read()) << 8) | uint16_t(Wire.read());
  return true;
}
bool convert(uint16_t config, int16_t &raw) {
  if (!writeRegister(1, config)) return false;
  const uint32_t started = millis();
  while (millis() - started < 50) {
    uint16_t state;
    if (!readRegister(1, state)) return false;
    if (state & 0x8000) {
      uint16_t result;
      if (!readRegister(0, result)) return false;
      raw = int16_t(result); return true;
    }
    delay(1);
  }
  return false;
}
bool refreshSupply() {
  // AIN2's 100k/100k divider is a slow, high-impedance monitor. Wait for
  // RC settling, then average repeated conversions; calibrate with the same
  // assembled divider because the ADC input loading affects its scale.
  int16_t raw;
  if (!convert(fft_force::kSupplyConfig128, raw)) {supplyValid = false; return false;}
  delay(30);
  int32_t sum = 0;
  for (int i=0;i<4;++i) {if (!convert(fft_force::kSupplyConfig128, raw)) {supplyValid=false;return false;} sum += raw;}
  supplyRaw = int16_t(sum / 4);
  lastSupplyMs = millis();
  supplyValid = supplyRaw > 0;
  supplyFresh = true;
  return supplyValid;
}
bool sample(int16_t &raw, double &ratio, double &excitationV) {
  if (!supplyValid || millis()-lastSupplyMs > 1000) if (!refreshSupply()) return false;
  // Deliberately wait a full single conversion after each mux/gain change.
  if (!convert(fft_force::kForceConfig128, raw)) return false;
  return fft_force::normalized(raw, supplyRaw, ratio, excitationV);
}
bool average(double &mean, double &sigma) {
  if (!refreshSupply()) return false;
  double sum=0.0, sumSquares=0.0;
  constexpr int kCount=64;
  for (int i=0;i<kCount;++i) {
    int16_t raw; double ratio, excitation;
    if (!sample(raw,ratio,excitation)) return false;
    sum+=ratio; sumSquares+=ratio*ratio;
  }
  mean=sum/kCount;
  sigma=sqrt(fmax(0.0,sumSquares/kCount-mean*mean));
  return true;
}
void saveCalibration() {
  preferences.putBool("zero_valid",calibration.zeroValid);
  preferences.putBool("span_valid",calibration.spanValid);
  preferences.putDouble("zero",calibration.zeroRatio);
  preferences.putDouble("scale",calibration.newtonsPerRatio);
}
void handleCommand(String line) {
  line.trim();
  if (line.equalsIgnoreCase("zero")) {
    double mean,sigma;
    if (!average(mean,sigma)) {Serial.println("ERR,zero_sample_failed");return;}
    calibration.zeroRatio=mean;calibration.zeroValid=true;saveCalibration();
    Serial.printf("ZERO,ratio=%.9f,sigma=%.9f\n",mean,sigma);
  } else if (line.startsWith("cal ")) {
    const double knownForce=line.substring(4).toDouble();
    double mean,sigma;
    if (!calibration.zeroValid) {Serial.println("ERR,run_zero_unloaded_first");return;}
    if (!average(mean,sigma)||!calibration.setSpan(mean,knownForce)) {Serial.println("ERR,invalid_load_or_span_use_0_to_8_N");return;}
    saveCalibration();
    Serial.printf("CAL,known_N=%.5f,ratio=%.9f,scale=%.6f,sigma=%.9f\n",knownForce,mean,calibration.newtonsPerRatio,sigma);
  } else if (line.equalsIgnoreCase("clear")) {
    calibration={};preferences.clear();Serial.println("CAL,cleared");
  } else if (line.equalsIgnoreCase("info")) {
    Serial.printf("INFO,address=0x48,GPIO_SDA=%d,GPIO_SCL=%d,rate_target=128,zero=%d,span=%d,zero_ratio=%.9f,scale=%.6f\n",kSda,kScl,calibration.zeroValid,calibration.spanValid,calibration.zeroRatio,calibration.newtonsPerRatio);
  } else if (line.length()) Serial.println("HELP,zero | cal <known_force_N> | clear | info");
}
}

void setup() {
  // Known FFT outputs default inactive during this measurement-only build.
  for (int pin : {fft_pins::kBacklightPwm,fft_pins::kFfsPowerEnable,fft_pins::kFfsLinkEnable,fft_pins::kLcdReset}) {digitalWrite(pin,LOW);pinMode(pin,OUTPUT);}
  // External5V revision: battery/charger/MP28167 controls are removed.
  // FFS_OC_N remains an externally biased3.18V input.
  // GPIO11 is unused/NC after the2026-10-22 USB-C-to-JDBG change.
  for (int pin : {fft_pins::kFfsInterrupt,fft_pins::kFfsReset,fft_pins::kFfsOvercurrent,fft_pins::kWakeKey,fft_pins::kLcdSck,fft_pins::kLcdMosi,fft_pins::kLcdDc}) pinMode(pin,INPUT);
  digitalWrite(fft_pins::kLcdCs,HIGH);pinMode(fft_pins::kLcdCs,OUTPUT);
  Serial.begin(115200);
  Wire.begin(kSda,kScl,100000);Wire.setTimeOut(50);
  preferences.begin("fft_force",false);
  calibration.zeroValid=preferences.getBool("zero_valid",false);
  calibration.spanValid=preferences.getBool("span_valid",false);
  calibration.zeroRatio=preferences.getDouble("zero",0.0);
  calibration.newtonsPerRatio=preferences.getDouble("scale",0.0);
  if (!std::isfinite(calibration.zeroRatio)||!std::isfinite(calibration.newtonsPerRatio)||calibration.newtonsPerRatio<=0) calibration.spanValid=false;
  delay(1000);
  Serial.println("FFT_TACTILE_BENCH,1");
  Serial.println("HELP,unload_then_zero; apply_known_reference_force_then_cal_N; calibrated_newtons_only_after_span");
  Serial.println("CSV,ms,raw,differential_mV,excitation_est_V,normalized_mV_per_V,force_N,calibrated,supply_fresh");
}

void loop() {
  while (Serial.available()) {
    const char c=char(Serial.read());
    if(c=='\n'||c=='\r'){handleCommand(command);command="";}
    else if(command.length()<64)command+=c;
  }
  int16_t raw;double ratio,excitation;
  if(sample(raw,ratio,excitation)) {
    Serial.printf("CSV,%lu,%d,%.6f,%.6f,%.6f,%.6f,%d,%d\n",static_cast<unsigned long>(millis()),raw,double(raw)*fft_force::kForceLsbV*1000,excitation,ratio*1000,calibration.force(ratio),calibration.spanValid&&calibration.zeroValid,supplyFresh);
    supplyFresh=false;
  } else {
    if(millis()-lastMissingMs>1000){Serial.println("ERR,adc_absent_timeout_bad_supply_or_saturation");lastMissingMs=millis();}
    delay(10);
  }
}
