#pragma once

#include "Metal.hpp"
#include <string>
#include <vector>

namespace dxmt {

class HUDState {
public:
  void initialize(const std::string &heading);

  void begin();

  void
  printLine(const std::string &str) {
    printLine(str.c_str());
  }

  void printLine(const char *c_str);

  void end();

  HUDState(WMT::DeveloperHUDProperties hud) : hud_(hud){};

private:
  WMT::DeveloperHUDProperties hud_;
  std::vector<WMT::Reference<WMT::String>> line_labels_;
  WMT::Reference<WMT::Object> pool_;
  unsigned current_line_;
};

} // namespace dxmt