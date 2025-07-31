#include "dxmt_hud_state.hpp"
#include "Metal.hpp"
#include <string>

namespace dxmt {

void
HUDState::initialize(const std::string &heading) {
  using namespace WMT;
  auto pool = MakeAutoreleasePool();
  auto str_dxmt_version = MakeString("com.github.3shain.dxmt-version", WMTUTF8StringEncoding);
  hud_.addLabel(str_dxmt_version, String::string("com.apple.hud-graph.default", WMTUTF8StringEncoding));
  hud_.updateLabel(str_dxmt_version, String::string(heading.c_str(), WMTUTF8StringEncoding));
  line_labels_.push_back(std::move(str_dxmt_version));
}

void
HUDState::begin() {
#ifndef DXMT_DEBUG
  return;
#endif
  pool_ = WMT::MakeAutoreleasePool();
  current_line_ = 1;
}

void
HUDState::printLine(const char *c_str) {
#ifndef DXMT_DEBUG
  return;
#endif
  using namespace WMT;
  while (current_line_ >= line_labels_.size()) {
    String prev = line_labels_.back();
    line_labels_.push_back(MakeString(
        ("com.github.3shain.dxmt-line" + std::to_string(line_labels_.size())).c_str(), WMTUTF8StringEncoding
    ));
    hud_.addLabel(line_labels_.back(), prev);
  }
  hud_.updateLabel(line_labels_[current_line_], String::string(c_str, WMTUTF8StringEncoding));
  current_line_++;
}

void
HUDState::end() {
#ifndef DXMT_DEBUG
  return;
#endif
  while (line_labels_.size() > current_line_) {
    hud_.remove(line_labels_.back());
    line_labels_.pop_back();
  }
  pool_ = nullptr;
}

} // namespace dxmt