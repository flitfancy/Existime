#pragma once

#include "config.h"

void collectorSetup();
void collectorTick();

bool collectorRunning();
void collectorStop();
void collectorSetMode(CollectorMode mode);
CollectorMode collectorGetMode();
void collectorEmitNow();

// Latest CH3 LTR390 ambient-light raw count. Returns false when no fresh
// reading exists yet (boot warm-up or sensor failure); callers keep their
// current light state instead of guessing.
bool collectorLatestAls(uint32_t& alsRaw);

// Main-loop-only, read-only snapshot of an already acquired physical channel.
// Clears out and returns false while stopped, missing, recovering or stale.
// Does not perform I2C transactions or alter the sensor/reporting schedule.
struct SensorValues;
bool collectorLatestSensor(uint8_t channel, SensorValues& out);

// 1 MB bounded CSV log on LittleFS; restarts cleanly at the size limit.
bool ringLogLine(const String& line);
