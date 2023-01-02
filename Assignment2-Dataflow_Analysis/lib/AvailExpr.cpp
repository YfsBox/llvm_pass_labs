/**
 * @file Available Expression Dataflow Analysis
 */
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>

#include <dfa/Framework.h>
#include <dfa/MeetOp.h>

#include "Expression.h"
#include "Variable.h"

using namespace dfa;
using namespace llvm;

namespace {

using AvailExprFrameworkBase =
    Framework<Expression, bool, Direction::kForward, Intersect>;

class AvailExpr final : public AvailExprFrameworkBase, public FunctionPass {
private:
  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      /**
       * @todo(cscd70) Please complete the construction of domain.
       */
       Domain.emplace_back(*BinaryOp);
    }
  }
  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IBV,
                            DomainVal_t &OBV) override {      // 对于某一个Instruction的遍历，如果发生改变就返回true
    /**
     * @todo(cscd70) Please complete the definition of the transfer function.
     */
    DomainVal_t old_out = OBV;
    // 首先求出由gen和kill所组成的集合
    OBV = IBV;
    unsigned operand_size = Inst.getNumOperands();
    for (unsigned i = 0; i < operand_size; ++i) {
        auto operand_ins = dyn_cast<Instruction>(Inst.getOperand(i));
        if (operand_ins) {      // 可以为kill
            auto map_idx = InstIndexMap[operand_ins];
            OBV[map_idx] = false; // 相当于将kill从in中去除了
        }
    }
    auto egen_idx = InstIndexMap[&Inst];
    OBV[egen_idx] = true;
    // 之后在进行运算，并比对结果
    unsigned domainval_size = IBV.size();
    for (unsigned i = 0; i < domainval_size; ++i) {
        if (OBV[i] != old_out[i]) {
            return true;
        }
    }
    return false;
  }

public:
  static char ID;

  AvailExpr() : AvailExprFrameworkBase(), FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
  bool runOnFunction(Function &F) override {
    return AvailExprFrameworkBase::runOnFunction(F);
  }
};

char AvailExpr::ID = 0;
RegisterPass<AvailExpr> X("avail-expr", "Available Expression");

} // anonymous namespace
