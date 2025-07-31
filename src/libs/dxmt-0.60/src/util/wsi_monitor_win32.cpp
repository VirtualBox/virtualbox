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

#include "log/log.hpp"
#include "util_string.hpp"

#include <cstring>

#include <setupapi.h>
#include <ntddvdeo.h>
#include <cfgmgr32.h>

namespace dxmt::wsi {

HMONITOR getDefaultMonitor() {
  return ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

struct MonitorEnumInfo {
  UINT iMonitorId;
  HMONITOR oMonitor;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hmon, HDC hdc, LPRECT rect,
                                     LPARAM lp) {
  auto data = reinterpret_cast<MonitorEnumInfo *>(lp);

  if (data->iMonitorId--)
    return TRUE; /* continue */

  data->oMonitor = hmon;
  return FALSE; /* stop */
}

HMONITOR enumMonitors(uint32_t index) {
  MonitorEnumInfo info;
  info.iMonitorId = index;
  info.oMonitor = nullptr;

  ::EnumDisplayMonitors(nullptr, nullptr, &MonitorEnumProc,
                        reinterpret_cast<LPARAM>(&info));

  return info.oMonitor;
}

bool getDisplayName(HMONITOR hMonitor, WCHAR (&Name)[32]) {
  // Query monitor info to get the device name
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Win32 WSI: getDisplayName: Failed to query monitor info");
    return false;
  }

  std::memcpy(Name, monInfo.szDevice, sizeof(Name));

  return true;
}

bool getDesktopCoordinates(HMONITOR hMonitor, RECT *pRect) {
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Win32 WSI: getDisplayName: Failed to query monitor info");
    return false;
  }

  *pRect = monInfo.rcMonitor;

  return true;
}

static inline void convertMode(const DEVMODEW &mode, WsiMode *pMode) {
  pMode->width = mode.dmPelsWidth;
  pMode->height = mode.dmPelsHeight;
  pMode->refreshRate = WsiRational{mode.dmDisplayFrequency * 1000, 1000};
  pMode->bitsPerPixel = mode.dmBitsPerPel;
  pMode->interlaced = mode.dmDisplayFlags & DM_INTERLACED;
}

static inline bool retrieveDisplayMode(HMONITOR hMonitor, DWORD modeNumber,
                                       WsiMode *pMode) {
  // Query monitor info to get the device name
  ::MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);

  if (!::GetMonitorInfoW(hMonitor, reinterpret_cast<MONITORINFO *>(&monInfo))) {
    Logger::err("Win32 WSI: retrieveDisplayMode: Failed to query monitor info");
    return false;
  }

  DEVMODEW devMode = {};
  devMode.dmSize = sizeof(devMode);

  if (!::EnumDisplaySettingsW(monInfo.szDevice, modeNumber, &devMode))
    return false;

  convertMode(devMode, pMode);

  return true;
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

static std::wstring getMonitorDevicePath(HMONITOR hMonitor) {
  // Get the device name of the monitor.
  MONITORINFOEXW monInfo;
  monInfo.cbSize = sizeof(monInfo);
  if (!::GetMonitorInfoW(hMonitor, &monInfo)) {
    Logger::err("getMonitorDevicePath: Failed to get monitor info.");
    return {};
  }

  // Try and find the monitor device path that matches
  // the name of our HMONITOR from the monitor info.
  LONG result = ERROR_SUCCESS;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths;
  std::vector<DISPLAYCONFIG_MODE_INFO> modes;
  do {
    uint32_t pathCount = 0, modeCount = 0;
    if ((result = ::GetDisplayConfigBufferSizes(
             QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount)) != ERROR_SUCCESS) {
      Logger::err(str::format(
          "getMonitorDevicePath: GetDisplayConfigBufferSizes failed. ret: ",
          result, " LastError: ", GetLastError()));
      return {};
    }
    paths.resize(pathCount);
    modes.resize(modeCount);
    result =
        ::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                             &modeCount, modes.data(), nullptr);
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS) {
    Logger::err(str::format(
        "getMonitorDevicePath: QueryDisplayConfig failed. ret: ", result,
        " LastError: ", GetLastError()));
    return {};
  }

  // Link a source name -> target name
  for (const auto &path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
    sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size = sizeof(sourceName);
    sourceName.header.adapterId = path.sourceInfo.adapterId;
    sourceName.header.id = path.sourceInfo.id;
    if ((result = ::DisplayConfigGetDeviceInfo(&sourceName.header)) !=
        ERROR_SUCCESS) {
      Logger::err(
          str::format("getMonitorDevicePath: DisplayConfigGetDeviceInfo with "
                      "DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME failed. ret: ",
                      result, " LastError: ", GetLastError()));
      continue;
    }

    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName;
    targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size = sizeof(targetName);
    targetName.header.adapterId = path.targetInfo.adapterId;
    targetName.header.id = path.targetInfo.id;
    if ((result = ::DisplayConfigGetDeviceInfo(&targetName.header)) !=
        ERROR_SUCCESS) {
      Logger::err(
          str::format("getMonitorDevicePath: DisplayConfigGetDeviceInfo with "
                      "DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME failed. ret: ",
                      result, " LastError: ", GetLastError()));
      continue;
    }

    // Does the source match the GDI device we are looking for?
    // If so, return the target back.
    if (!wcscmp(sourceName.viewGdiDeviceName, monInfo.szDevice))
      return targetName.monitorDevicePath;
  }

  Logger::err(
      "getMonitorDevicePath: Failed to find a link from source -> target.");
  return {};
}

} // namespace dxmt::wsi