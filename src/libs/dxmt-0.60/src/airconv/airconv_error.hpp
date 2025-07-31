#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
namespace dxmt {

class UnsupportedFeature : public llvm::ErrorInfo<UnsupportedFeature> {
public:
  static char ID;
  std::string msg;

  UnsupportedFeature(llvm::StringRef msg) : msg(msg.str()) {}

  void log(llvm::raw_ostream &OS) const override { OS << msg; }

  std::error_code convertToErrorCode() const override {
    return std::make_error_code(std::errc::not_supported);
  }
};
}; // namespace dxmt