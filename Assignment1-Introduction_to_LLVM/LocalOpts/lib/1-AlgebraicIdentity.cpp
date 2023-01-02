#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <list>

using namespace llvm;

namespace {

class AlgebraicIdentity final : public FunctionPass {

private:
    static ConstantInt *getConstIntOperand(const Instruction &ins, int *other_idx);
public:
  static char ID;

  AlgebraicIdentity() : FunctionPass(ID) {}

  /**
   * @todo(cscd70) Please complete the methods below.
   */
  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
  }

  virtual bool runOnFunction(Function &F) override {
      std::list<Instruction*> dele_list;
      for (auto &bb : F) {
          for (auto &ins : bb) {  // 遍历指令
              // 判断操作类型
              if (ins.isBinaryOp()) {
                  auto optype = ins.getOpcode();
                  if (optype == Instruction::Add) {
                      int the_other_idx;
                      auto constint = getConstIntOperand(ins, &the_other_idx);
                      if (constint == nullptr) {
                          continue;
                      }
                      if (constint->getSExtValue() == 0) {
                          ins.replaceAllUsesWith(ins.getOperand(the_other_idx));
                          dele_list.push_back(&ins);
                      }
                  } else if (optype == Instruction::Mul) {
                      int the_other_idx;
                      auto constint = getConstIntOperand(ins, &the_other_idx);
                      if (constint == nullptr) {
                          continue;
                      }

                      if (constint->getSExtValue() == 1) {
                          ins.replaceAllUsesWith(ins.getOperand(the_other_idx));
                          dele_list.push_back(&ins);
                      }
                  }
              }
          }
      }
      for (auto dele_ins : dele_list) {
          dele_ins->eraseFromParent();
      }
      return false;
  }
}; // class AlgebraicIdentity

ConstantInt *AlgebraicIdentity::getConstIntOperand(const Instruction &ins, int *other_idx) {
    auto operand0 = dyn_cast<ConstantInt>(ins.getOperand(0));
    if (operand0) {
        *other_idx = 1;
        return operand0;
    }
    auto operand1 = dyn_cast<ConstantInt>(ins.getOperand(1));
    *other_idx = 0;
    return operand1;
}

char AlgebraicIdentity::ID = 0;
RegisterPass<AlgebraicIdentity> X("algebraic-identity",
                                  "CSCD70: Algebraic Identity");

} // anonymous namespace


// 为什么会有Operand为nullptr的情况出现?

