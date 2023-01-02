#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

using namespace llvm;

namespace {

class StrengthReduction final : public FunctionPass {
private:
    static ConstantInt *getConstIntOperand(const Instruction &ins, int *other_idx);
public:
  static char ID;
  StrengthReduction() : FunctionPass(ID) {}

  /**
   * @todo(cscd70) Please complete the methods below.
   */
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
  }

  virtual bool runOnFunction(Function &F) override {
      std::list<std::pair<Instruction*, int>> replace_inst_list;
      for (auto &bb : F) {
          for (auto &ins : bb) {
              if (ins.isBinaryOp() && ins.getOpcode() == Instruction::Mul) {
                  int theother_idx;
                  auto constint = getConstIntOperand(ins, &theother_idx);
                  if (constint == nullptr) {
                      continue;
                  }
                  if (constint->getZExtValue() == 16) {
                      replace_inst_list.push_back({&ins, theother_idx});
                  }
              }
          }
      }
      for (auto &inst : replace_inst_list) {
          Instruction *ins = inst.first;
          int other_operand_idx = inst.second;
          IRBuilder<> builder(inst.first);
          auto value = builder.CreateShl(ins->getOperand(other_operand_idx), 4);
          ins->replaceAllUsesWith(value);
          ins->eraseFromParent();
      }
      return false;
  }
}; // class StrengthReduction

ConstantInt *StrengthReduction::getConstIntOperand(const Instruction &ins, int *other_idx) {
    auto operand0 = dyn_cast<ConstantInt>(ins.getOperand(0));
    if (operand0) {
        *other_idx = 1;
        return operand0;
    }
    auto operand1 = dyn_cast<ConstantInt>(ins.getOperand(1));
    *other_idx = 0;
    return operand1;
}



char StrengthReduction::ID = 0;
RegisterPass<StrengthReduction> X("strength-reduction",
                                  "CSCD70: Strength Reduction");

} // anonymous namespace
