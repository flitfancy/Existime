#pragma once

#include <Arduino.h>

void tftDisplaySetup();
void tftDisplayTick();
bool tftDisplayCommand(const String& command);
String tftDisplayJson();
