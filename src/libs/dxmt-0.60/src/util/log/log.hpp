/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

#pragma once

#include <fstream>
#include <string>

#include "../thread.hpp"
#include "../util_string.hpp"

namespace dxmt {

enum class LogLevel : uint32_t {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  None = 5,
};

using PFN_wineLogOutput = int(__cdecl *)(const char *);

/**
 * \brief Logger
 *
 * Logger for one DLL. Creates a text file and
 * writes all log messages to that file.
 */
class Logger {

public:
  Logger(const std::string &file_name);
  ~Logger();

  static void trace(const std::string &message);
  static void debug(const std::string &message);
  static void info(const std::string &message);
  static void warn(const std::string &message);
  static void err(const std::string &message);
  static void log(LogLevel level, const std::string &message);

#ifndef VBOX
  static LogLevel logLevel() { return s_instance.m_minLevel; }
#else
  static LogLevel logLevel() { return LogLevel::Info; }
#endif

private:
  static Logger s_instance;

  const LogLevel m_minLevel;
  const std::string m_fileName;

  dxmt::mutex m_mutex;
  std::ofstream m_fileStream;

  bool m_initialized = false;
  PFN_wineLogOutput m_wineLogOutput = nullptr;

#ifndef VBOX
  void emitMsg(LogLevel level, const std::string &message);
#endif

  std::string getFileName(const std::string &base);

  static LogLevel getMinLogLevel();
};

} // namespace dxmt


#define TRACE(...) Logger::trace(str::format(__VA_ARGS__))

#define WARN(...) Logger::warn(str::format(__VA_ARGS__))

#define ERR(...) Logger::err(str::format(__VA_ARGS__))

#define ERR_ONCE(...)                                                          \
  static bool s_errorShown = false;                                            \
  if (!std::exchange(s_errorShown, true)) {                                    \
    Logger::err(str::format(__VA_ARGS__));                                     \
  }
