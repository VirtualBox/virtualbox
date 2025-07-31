/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#include "util_string.hpp"
#include <array>
#include <cstdlib>
#include <numeric>
#include <filesystem>

#ifdef __linux__
#include <unistd.h>
#include <limits.h>
#endif

#include "util_env.hpp"
#include "unknwn.h"

namespace dxmt::env {

std::string getEnvVar(const char *name) {
#ifdef _WIN32
  std::vector<WCHAR> result;
  result.resize(MAX_PATH + 1);

  DWORD len = ::GetEnvironmentVariableW(str::tows(name).c_str(), result.data(),
                                        MAX_PATH);
  result.resize(len);

  return str::fromws(result.data());
#else
  const char *result = std::getenv(name);
  return result ? result : "";
#endif
}

size_t matchFileExtension(const std::string &name, const char *ext) {
  auto pos = name.find_last_of('.');

  if (pos == std::string::npos)
    return pos;

  bool matches = std::accumulate(name.begin() + pos + 1, name.end(), true,
                                 [&ext](bool current, char a) {
                                   if (a >= 'A' && a <= 'Z')
                                     a += 'a' - 'A';
                                   return current && *ext && a == *(ext++);
                                 });

  return matches ? pos : std::string::npos;
}

std::string getExeName() {
  std::string fullPath = getExePath();
  auto n = fullPath.find_last_of(env::PlatformDirSlash);

  return (n != std::string::npos) ? fullPath.substr(n + 1) : fullPath;
}

std::string getExeBaseName() {
  auto exeName = getExeName();
#ifdef _WIN32
  auto extp = matchFileExtension(exeName, "exe");

  if (extp != std::string::npos)
    exeName.erase(extp);
#endif

  return exeName;
}

std::string getExePath() {
#if defined(_WIN32)
  std::vector<WCHAR> exePath;
  exePath.resize(MAX_PATH + 1);

  DWORD len = ::GetModuleFileNameW(NULL, exePath.data(), MAX_PATH);
  exePath.resize(len);

  return str::fromws(exePath.data());
#elif defined(__linux__)
  std::array<char, PATH_MAX> exePath = {};

  size_t count = readlink("/proc/self/exe", exePath.data(), exePath.size());

  return std::string(exePath.begin(), exePath.begin() + count);
#else
  return std::string(""); //TODO
#endif
}

void setThreadName(const std::string &name) {
#ifdef _WIN32
  using SetThreadDescriptionProc = HRESULT(WINAPI *)(HANDLE, PCWSTR);

  static auto SetThreadDescription =
      reinterpret_cast<SetThreadDescriptionProc>(::GetProcAddress(
          ::GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));

  if (SetThreadDescription) {
    std::array<wchar_t, 16> wideName = {};

    str::transcodeString(wideName.data(), wideName.size() - 1, name.data(),
                         name.size());

    SetThreadDescription(::GetCurrentThread(), wideName.data());
  }
#else
  std::array<char, 16> posixName = {};
  dxmt::str::strlcpy(posixName.data(), name.c_str(), 16);
  ::pthread_setname_np(posixName.data());
#endif
}

bool createDirectory(const std::string &path) {
#ifdef _WIN32
  std::array<WCHAR, MAX_PATH + 1> widePath;

  size_t length = str::transcodeString(widePath.data(), widePath.size() - 1,
                                       path.data(), path.size());

  widePath[length] = L'\0';
  return !!CreateDirectoryW(widePath.data(), nullptr);
#else
  return std::filesystem::create_directories(path);
#endif
}

std::string getUnixPath(const std::string &path) {
#if defined(_WIN32)
  using GetUnixFileName = LPSTR (*CDECL)(LPCWSTR);

  static auto wine_get_unix_file_name =
      reinterpret_cast<GetUnixFileName>(::GetProcAddress(
          ::GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name"));

  if (wine_get_unix_file_name) {
    std::array<WCHAR, MAX_PATH + 1> widePath;

    size_t length = str::transcodeString(widePath.data(), widePath.size() - 1,
                                       path.data(), path.size());

    widePath[length] = L'\0';
    return std::string(wine_get_unix_file_name(widePath.data()));
  }
  return "";
#else
  return path;
#endif
}

} // namespace dxmt::env
