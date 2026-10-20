# Firefly ST7735

Derived from Adafruit ST7735 and ST7789 Library 1.11.0, https://github.com/adafruit/Adafruit-ST7735-Library. Original Adafruit authorship and MIT license notices are retained in each source file.

Only ST7735 and its shared ST77xx base are included. The local change adds setInitSPISpeed and passes it to begin from commonInit. Command tables, pin handling, profiles, colors and rotation remain upstream code. This makes low-rate initialization reproducible without editing the installed dependency cache.
The 1.3.12 fix clears retained GREEN offsets before initR/initB so repeated
profile changes do not leave stale row/column offsets. Command tables and
rotation/color selection remain upstream.
The 1.3.10 diagnostic addition exposes read-only pointers to the unchanged
GREEN initialization tables. A separate GPIO probe replays those tables with
explicit clock delays; the normal Adafruit drawing path is unchanged.
