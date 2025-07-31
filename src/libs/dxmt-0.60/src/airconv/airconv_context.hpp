#pragma once
#include "llvm/IR/Module.h"
#include "llvm/Passes/OptimizationLevel.h"

namespace dxmt {

struct ModuleOptions {
  bool enableFastMath;
};

void initializeModule(llvm::Module &M, const ModuleOptions &opts);

void runOptimizationPasses(llvm::Module &M, llvm::OptimizationLevel opt);
} // namespace dxmt