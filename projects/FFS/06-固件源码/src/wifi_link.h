#pragma once

#include <Arduino.h>

void wifiSetup();
void wifiTick();
void wifiSendLine(const String& line);
bool wifiLinkConfirmed();

// 当前 WiFi RSSI（dBm）；未连接时为 0。随 CSV 上报用于断联诊断。
int wifiRssiDbm();

// Returns one pending command received over TCP ("" when none).
String wifiTakeCommand();

void wifiStatusPrint();
void wifiReconnect();
