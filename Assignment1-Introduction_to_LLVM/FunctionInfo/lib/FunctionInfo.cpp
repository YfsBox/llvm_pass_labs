#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace {

class FunctionInfo final : public ModulePass {
private:
    void showFuncInfo(const Function &func);
public:
  static char ID;

  FunctionInfo() : ModulePass(ID) {}

  // We don't modify the program, so we preserve all analysis.
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  virtual bool runOnModule(Module &M) override {
    outs() << "CSCD70 Function Information Pass"
           << "\n";
    /**
     * @todo(cscd70) Please complete this method.
     */
    for (auto &func : M) {
        showFuncInfo(func);
    }
    return false;
  }
}; // class FunctionInfo

void FunctionInfo::showFuncInfo(const Function &func) {
    func.arg_size();
    outs() << "#Name:" << func.getName() << "\nArgs:" << func.arg_size() << "\nCalls:"
            << func.getNumUses() << "\nBlocks:" << func.size() << "\nInsts:"
            << func.getInstructionCount() << "\n";
}

char FunctionInfo::ID = 0;
RegisterPass<FunctionInfo> X("function-info", "CSCD70: Function Information");

} // anonymous namespace
