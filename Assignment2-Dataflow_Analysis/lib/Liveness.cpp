/**
 * @file Liveness Dataflow Analysis
 */
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#include "dfa/Framework.h"
#include "dfa/MeetOp.h"
#include <map>

#include "Variable.h"

using namespace dfa;
using namespace llvm;

namespace {
    using LivenessFrameworkBase =
            Framework<Variable, bool, Direction::kBackward, Union>;
/**
 * @todo(cscd70) Implement @c Liveness using the @c dfa::Framework interface.
 */
class Liveness final : public LivenessFrameworkBase, public FunctionPass {
private:
    virtual void initializeDomainFromInst(const Instruction &Inst) override {
        for (auto op_it = Inst.op_begin(); op_it != Inst.op_end(); ++op_it) {
            auto operand = op_it->get();
            if (isa<Argument>(operand) || isa<Instruction>(operand)) {
                auto findit = InstIndexMap.find(operand);
                if (findit == InstIndexMap.end()) {
                    InstIndexMap[operand] = Domain.size();
                    Domain.emplace_back(Variable(operand));
                }
            }
        }
    }

    virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IBV,
                              DomainVal_t &OBV) override;

public:
  static char ID;

  Liveness() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  virtual bool runOnFunction(Function &F) override {
    // clang-format off
    errs() << "**************************************************" << "\n"
           << "* Instruction-Domain Value Mapping" << "\n"
           << "**************************************************" << "\n";
    // clang-format on
    return LivenessFrameworkBase::runOnFunction(F);
  }
};

bool Liveness::transferFunc(const Instruction &Inst, const DomainVal_t &IBV, DomainVal_t &OBV) {
    std::map<unsigned, bool> OLD_OBV;
    OBV = IBV;
    // 首先复制好OLD_OBV
    for (size_t i = 0; i < IBV.size(); ++i) {
        OLD_OBV[i] = OBV[i];
    }
    // 将该Inst def的部分去除掉
    auto find_def_it = InstIndexMap.find(&Inst);
    if (find_def_it != InstIndexMap.end()) {  // 表示该指令存在一个def
        OBV[find_def_it->second] = false;
    }
    // 将use的部分合并上
    for (auto op_it = Inst.op_begin(); op_it != Inst.op_end(); ++op_it) {
        auto operand = op_it->get();
        if (isa<Argument>(operand) || isa<Instruction>(operand)) {
            auto findit = InstIndexMap.find(operand);
            if (findit != InstIndexMap.end()) {     // 如果该def之前是存在的话
                OBV[findit->second] = true;
            }
        }
    }
    for (size_t i = 0; i < IBV.size(); ++i) {
        errs() << OBV[i];
    }
    errs() << '\n';
    for (size_t i = 0; i < IBV.size(); ++i) {
        errs() << OLD_OBV[i];
    }
    errs() << '\n';

    for (size_t i = 0; i < IBV.size(); ++i) {
        if (OBV[i] != OLD_OBV[i]) {
            return true;
        }
    }
    return false;
}

char Liveness::ID = 1;
RegisterPass<Liveness> X("liveness", "Liveness");

} // anonymous namespace
