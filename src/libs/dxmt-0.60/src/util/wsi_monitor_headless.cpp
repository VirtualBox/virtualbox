/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include "wsi_monitor.hpp"

namespace dxmt::wsi {

HMONITOR getDefaultMonitor() {
  return nullptr;
}

HMONITOR enumMonitors(uint32_t index) {
  return nullptr;
}

bool getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) {
  return false;
}

bool getDesktopCoordinates(HMONITOR hMonitor, RECT *pRect) {
  return false;
}

static inline bool retrieveDisplayMode(HMONITOR hMonitor, DWORD modeNumber,
                                       WsiMode *pMode) {
  return false;
}

bool getDisplayMode(HMONITOR hMonitor, uint32_t modeNumber, WsiMode *pMode) {
  return retrieveDisplayMode(hMonitor, modeNumber, pMode);
}

bool getCurrentDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  return retrieveDisplayMode(hMonitor, ENUM_CURRENT_SETTINGS, pMode);
}

bool getDesktopDisplayMode(HMONITOR hMonitor, WsiMode *pMode) {
  return retrieveDisplayMode(hMonitor, ENUM_REGISTRY_SETTINGS, pMode);
}

} // namespace dxmt::wsi