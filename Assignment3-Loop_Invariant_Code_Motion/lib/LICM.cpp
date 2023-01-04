/**
 * @file Loop Invariant Code Motion
 */
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <unordered_set>

using namespace llvm;

namespace {

class LoopInvariantCodeMotion final : public LoopPass {
private:
    bool isInvariant(Instruction *const I, const LoopInfo &loopInfo);
    bool traversalLoop(Loop *L, const LoopInfo &loopInfo);  // 返回值表示的是这一轮循环有没有出现changed
    bool isInSet(const Instruction *I) {
        return InvariantSet.find(const_cast<Instruction*>(I)) != InvariantSet.end();
    }

    std::unordered_set<Instruction*> InvariantSet;
public:
  static char ID;

  LoopInvariantCodeMotion() : LoopPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    /**
     * @todo(cscd70) Request the dominator tree and the loop simplify pass.
     */
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesCFG();       // 保留之前所求出的数据流图的结果.
  }

  /**
   * @todo(cscd70) Please finish the implementation of this method.
   */
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM) override {
      if (L->getLoopPreheader() == nullptr) {
          return false;
      }
      LoopInfo &loopInfo = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
      // 首先需要从中找出循环不变量，通过遍历Instruction来确定， 收集到一个list中
      while (traversalLoop(L, loopInfo)) {

      }


      // 然后将循环不变量所对应的指令以单个block的形式移动到该Loop的开头

      // 首先需要创建一个block(根据list创建)，该blcok指向循环的header

      // 将原本header的前驱block指向移动为新创建的block

      return false;
  }
};

bool LoopInvariantCodeMotion::isInvariant(Instruction *const I, const LoopInfo& loopInfo) {

    bool IsVariable = true;
    auto isInSameLoop
    = [&](const Instruction* LInst, const Instruction* RInst) -> bool {
        return loopInfo.getLoopFor(LInst->getParent())
        == loopInfo.getLoopFor(RInst->getParent());
    };

    for (auto oper_it = I->op_begin(); oper_it != I->op_end(); ++oper_it) {
        bool op_is_const, op_is_inset, op_notsame_loop;
        op_is_const = isa<Constant>(oper_it);
        auto oper_inst = dyn_cast<Instruction>(oper_it);
        if (oper_inst != nullptr) {
            op_is_inset = isInSet(oper_inst);
        } else {
            op_is_inset = false;
        }
        // 判断循环所处的位置，是否位于循环外部
        op_notsame_loop = !isInSameLoop(I, oper_inst);
        IsVariable &= (op_is_inset || op_is_const || op_notsame_loop);
    }
    return IsVariable &&
    isSafeToSpeculativelyExecute(I) &&
    !I->mayReadFromMemory() &&
    !isa<LandingPadInst>(I);

}

bool LoopInvariantCodeMotion::traversalLoop(Loop *L, const LoopInfo &loopInfo) {
    bool is_changed = false;
    for (auto BB : L->getBlocks()) {
        for (auto &Inst : *BB) {
            if (isInvariant(&Inst, loopInfo)) {
                InvariantSet.insert(&Inst);     // 加入到集合中
                is_changed = true;
            }
        }
    }
    return is_changed;
}

char LoopInvariantCodeMotion::ID = 0;
RegisterPass<LoopInvariantCodeMotion> X("loop-invariant-code-motion",
                                        "Loop Invariant Code Motion");

} // anonymous namespace

// 关于Loop这个class及其相关class的分析
