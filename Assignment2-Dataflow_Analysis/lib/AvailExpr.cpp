/**
 * @file Available Expression Dataflow Analysis
 */
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>

#include "dfa/Framework.h"
#include "dfa/MeetOp.h"

#include "Expression.h"

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
        InstIndexMap[&Inst] = Domain.size();
        Domain.emplace_back(*BinaryOp);
    }
  }
  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IBV,
                            DomainVal_t &OBV) override {      // 对于某一个Instruction的遍历，如果发生改变就返回true
    /**
     * @todo(cscd70) Please complete the definition of the transfer function.
     */
    DomainVal_t OLD_OBV = OBV;
    OBV = IBV;
    for (auto expr : Domain) {
        if (expr.LHS == &Inst || expr.RHS == &Inst) {
            auto ins_idx = InstIndexMap[expr.binaryOperator];
            OBV[ins_idx] = false;
        }
    }
    auto find_egen_it = InstIndexMap.find(&Inst);
    if (find_egen_it != InstIndexMap.end()) {
        OBV[find_egen_it->second] = true;
    }
    for (size_t i = 0; i < IBV.size(); ++i) {
        if (OLD_OBV[i] != OBV[i]) {
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
