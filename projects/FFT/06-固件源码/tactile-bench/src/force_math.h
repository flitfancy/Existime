#pragma once
#include <cmath>
#include <cstdint>

namespace fft_force {
constexpr double kForceLsbV = 0.256 / 32768.0;
constexpr double kSupplyLsbV = 2.048 / 32768.0;
constexpr uint16_t kForceConfig128 = 0x8B83;  // AIN0-AIN1, +/-256mV, single shot, 128SPS, comparator disabled.
constexpr uint16_t kSupplyConfig128 = 0xE583; // AIN2-GND, +/-2.048V, single shot, 128SPS, comparator disabled.

inline bool normalized(int16_t forceRaw, int16_t supplyRaw, double &ratio, double &excitationV) {
  excitationV = double(supplyRaw) * kSupplyLsbV * 2.0;
  if (!std::isfinite(excitationV) || excitationV < 2.7 || excitationV > 3.6 || forceRaw <= -32760 || forceRaw >= 32760) return false;
  ratio = double(forceRaw) * kForceLsbV / excitationV;
  return std::isfinite(ratio);
}

struct Calibration {
  bool zeroValid = false;
  bool spanValid = false;
  double zeroRatio = 0.0;
  double newtonsPerRatio = 0.0;

  bool setSpan(double loadedRatio, double knownForceN) {
    const double delta = loadedRatio - zeroRatio;
    if (!zeroValid || !std::isfinite(loadedRatio) || !std::isfinite(knownForceN) || knownForceN <= 0.0 || knownForceN > 8.0 || delta <= 1e-5) return false;
    newtonsPerRatio = knownForceN / delta;
    spanValid = std::isfinite(newtonsPerRatio);
    return spanValid;
  }
  double force(double ratio) const {
    return spanValid && zeroValid ? (ratio - zeroRatio) * newtonsPerRatio : NAN;
  }
};
}
