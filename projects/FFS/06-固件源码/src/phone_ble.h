#pragma once
#include <Arduino.h>

// Setup is called once after the shared NimBLE stack was initialized by the heart task.
bool phoneBleSetup();
void phoneBleTick();
bool phoneBleReady();
String phoneBleJson();
