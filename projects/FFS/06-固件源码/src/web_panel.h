#pragma once

#include <Arduino.h>

void webPanelSetup();
void webPanelTick();

// Collector feeds every CSV row here (ring buffer of recent rows).
void webPanelAddRow(const String& line);

// Called only from the sampling/main loop: no row writer can interleave the copy.
size_t webPanelSnapshotBytes();
bool webPanelCopyRows(char* buffer, size_t capacity, size_t* offsets, size_t maxRows,
                     size_t& rows, uint32_t& uptime);
