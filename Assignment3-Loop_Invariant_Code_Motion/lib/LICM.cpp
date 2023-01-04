/**
 * @file Loop Invariant Code Motion
 */
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <unordered_set>

using namespace llvm;

namespace {

class LoopInvariantCodeMotion final : public LoopPass {
private:
    bool isInvariant(const Loop *loop, Instruction *const I);
    bool traversalLoop(Loop *L);  // 返回值表示的是这一轮循环有没有出现changed
    bool isInSet(const Instruction *I) {
        return InvariantSet.find(const_cast<Instruction*>(I)) != InvariantSet.end();
    }
    bool checkIsExitsDom(const Loop *loop, const Instruction *Inst);
    void moveInvariant(const Loop *loop, Instruction *Inst);
    const LoopInfo *loopInfo;
    const DominatorTree *dominatorTree;
    std::unordered_set<Instruction*> InvariantSet;  // 这个map应该只让其作用于某一个Loop还是整个Function都有效呢？应该作为每次的Pass也是可以的吧
public:
  static char ID;

  LoopInvariantCodeMotion() : LoopPass(ID), loopInfo(nullptr), dominatorTree(nullptr) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    /**
     * @todo(cscd70) Request the dominator tree and the loop simplify pass.
     */
    AU.addRequired<DominatorTreeWrapperPass>();     // 用于检查某个Instruction是否是支配结点
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
      InvariantSet.clear();
      if (loopInfo == nullptr) {
          loopInfo = &(getAnalysis<LoopInfoWrapperPass>().getLoopInfo());
      }
      if (dominatorTree == nullptr) {
          dominatorTree = &(getAnalysis<DominatorTreeWrapperPass>().getDomTree());
      }
      // 首先需要从中找出循环不变量，通过遍历Instruction来确定， 收集到一个list中
      while (traversalLoop(L)) {
      }
      for (auto Invariant : InvariantSet) { // 将对应的指令移动到Preheader中
          // 需要判断的是是否是exit的支配结点
          if (checkIsExitsDom(L, Invariant)) {
              moveInvariant(L, Invariant);
          }
      }
      // 接下来根据所收集的指令判断是否可以并进行移动
      return false;
  }
};

bool LoopInvariantCodeMotion::isInvariant(const Loop *loop, Instruction *const I) { // loop 也正是当前所处理的I所处于loop

    bool IsVariable = true;
    auto isOutLoop
    = [&](const Instruction* inst) -> bool {
        return loop->contains(inst);// 用于判断是否处于统一循环中，从最深的一层开始
    };

    for (auto oper_it = I->op_begin(); oper_it != I->op_end(); ++oper_it) {
        bool op_is_const, op_is_inset, op_notsame_loop;
        op_is_const = isa<Constant>(oper_it) || isa<Argument>(oper_it);  // 常量和函数参数都是可以的
        auto oper_inst = dyn_cast<Instruction>(oper_it);
        if (oper_inst != nullptr) {
            op_is_inset = isInSet(oper_inst);
            op_notsame_loop = !isOutLoop(oper_inst);
        } else {
            op_is_inset = false;
            op_notsame_loop = false;
        }
        // 判断循环所处的位置，是否位于循环外部
        IsVariable &= (op_is_inset || op_is_const || op_notsame_loop);
    }
    return IsVariable &&
    isSafeToSpeculativelyExecute(I) &&
    !I->mayReadFromMemory() &&
    !isa<LandingPadInst>(I);

}

bool LoopInvariantCodeMotion::traversalLoop(Loop *L) {    // 每次只针对某一特定的L, 对应了runOnLoop中的L
    bool is_changed = false;
    for (auto BB : L->getBlocks()) {
        if (loopInfo->getLoopFor(BB) != L) {     // 每次只针对特定的L
            continue;
        }
        for (auto &Inst : *BB) {
            if (!isInSet(&Inst) && isInvariant(L, &Inst)) {
                InvariantSet.insert(&Inst);     // 加入到集合中
                is_changed = true;
            }
        }
    }
    return is_changed;
}

bool LoopInvariantCodeMotion::checkIsExitsDom(const Loop *loop, const Instruction *Inst) {
    // 在一个控制流图中, 其Exit Block往往有好几个
    SmallVector<BasicBlock*, 0> exit_blocks;
    loop->getExitBlocks(exit_blocks);
    for (auto exit_block : exit_blocks) {
        // 检查Inst是否为该Block的支配结点
        if (!dominatorTree->dominates(Inst, exit_block)) { // 如果不是支配结点
            return false;
        }
    }
    return true;
}

void LoopInvariantCodeMotion::moveInvariant(const Loop *loop, Instruction *Inst) {
    auto perheader = loop->getLoopPreheader();
    Inst->moveBefore(perheader->getTerminator());
}


char LoopInvariantCodeMotion::ID = 0;
RegisterPass<LoopInvariantCodeMotion> X("loop-invariant-code-motion",
                                        "Loop Invariant Code Motion");

} // anonymous namespace

// 关于Loop这个class及其相关class的分析
// 其中关于LoopPass是每次只处理一个Loop单元, Loop在一个程序中是一个区分层次的结构，在对内部的指令进行遍历时需要确保
// 处理的指令一定处于该Loop之下，如果不是就跳过。
//
