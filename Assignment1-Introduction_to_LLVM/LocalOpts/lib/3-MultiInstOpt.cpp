#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <list>

using namespace llvm;

namespace {

class MultiInstOpt final : public FunctionPass {
private:
    static ConstantInt *getConstIntOperand(const Instruction &ins, int *other_idx);
public:
  static char ID;

  MultiInstOpt() : FunctionPass(ID) {}

  /**
   * @todo(cscd70) Please complete the methods below.
   */
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
  }

  virtual bool runOnFunction(Function &F) override {
      std::list<Instruction*> delete_list;
      for (auto &bb : F) {
          for (auto &curr_ins : bb) {
              if (curr_ins.isBinaryOp() && curr_ins.getOpcode() == Instruction::Sub) {
                  int curr_other_idx;
                  auto curr_const_int = getConstIntOperand(curr_ins, &curr_other_idx);
                  if (curr_const_int == nullptr) {
                      continue;
                  }
                  auto other_ins = dyn_cast<Instruction>(curr_ins.getOperand(curr_other_idx));
                  if (other_ins == nullptr) {
                      continue;
                  }
                  if (!other_ins->isBinaryOp() || other_ins->getOpcode() != Instruction::Add) {
                      continue;
                  }
                  int pre_other_idx;
                  auto pre_const_int = getConstIntOperand(*other_ins, &pre_other_idx);
                  if (pre_const_int == nullptr) {
                      continue;
                  }
                  // 然后判断常数部分的值是否相等
                  if (pre_const_int->getSExtValue() == curr_const_int->getSExtValue()) { // 可以被优化
                      curr_ins.replaceAllUsesWith(curr_ins.getOperand(curr_other_idx));
                      delete_list.push_back(&curr_ins);
                  }
              }
          }
      }

      for (auto delete_ins : delete_list) {
          delete_ins->eraseFromParent();
      }
      return false;
  }
};  // class MultiInstOpt

ConstantInt *MultiInstOpt::getConstIntOperand(const Instruction &ins, int *other_idx) {
    auto operand0 = dyn_cast<ConstantInt>(ins.getOperand(0));
    if (operand0) {
        *other_idx = 1;
        return operand0;
    }
    auto operand1 = dyn_cast<ConstantInt>(ins.getOperand(1));
    *other_idx = 0;
    return operand1;
}

char MultiInstOpt::ID = 0;
RegisterPass<MultiInstOpt> X("multi-inst-opt",
                             "CSCD70: Multi-Instruction Optimization");

} // anonymous namespace
