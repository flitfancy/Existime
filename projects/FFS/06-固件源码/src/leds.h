#pragma once

#include "config.h"

bool ledInit();

// Channel activity LED (on while the channel is being read).
void ledSetChannel(uint8_t channel, bool on);

// Activity pulse tied to the real I2C operation. Short ready-status polls are
// stretched to a visible minimum without blocking the sampling scheduler.
void ledActivityStart(uint8_t channel);
void ledActivityFinish(uint8_t channel);
void ledUpdateActivity();
bool ledActivityBusy();

// Heartbeat LED (D4): off when linked, 1 s blink every 3 s when not.
void ledUpdateHeartbeat(bool linkConfirmed);

// Heart-rate LED (D8): equal bright/dark halves at one third of the fresh BPM frequency.
// Invalid/disconnected/stale measurements and dark mode keep it off.
void ledUpdateHeartRate(bool valid, uint16_t bpm);
// Diagnostic schedule and actual U5 output-register readback, not optical proof.
String ledHeartRateJson();

// Auto dark: when ambient light drops below the configured threshold the
// channel activity LEDs and D8 stay off regardless of
// collector state. Heartbeat LED is unaffected. Entering dark mode turns
// the decorative LEDs off immediately.
void ledSetDarkMode(bool dark);

// All controllable LEDs off.
void ledAllOff();
