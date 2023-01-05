/**
 * @file Interference Graph Register Allocator
 */
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/CodeGen/LiveIntervals.h>
#include <llvm/CodeGen/LiveRangeEdit.h>
#include <llvm/CodeGen/LiveRegMatrix.h>
#include <llvm/CodeGen/LiveStacks.h>
#include <llvm/CodeGen/MachineBlockFrequencyInfo.h>
#include <llvm/CodeGen/MachineDominators.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineLoopInfo.h>
#include <llvm/CodeGen/RegAllocRegistry.h>
#include <llvm/CodeGen/RegisterClassInfo.h>
#include <llvm/CodeGen/Spiller.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>
#include <llvm/CodeGen/VirtRegMap.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>

#include <cmath>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace llvm {

void initializeRAIntfGraphPass(PassRegistry &Registry);

} // namespace llvm

namespace std {

template <> //
struct hash<Register> {     // 表示寄存器的hash
  size_t operator()(const Register &Reg) const {
    return DenseMapInfo<Register>::getHashValue(Reg);
  }
};

template <> //
struct greater<LiveInterval *> {    // Live表示的变量存活时间间隔，可以用来比较大小
  bool operator()(LiveInterval *const &LHS, LiveInterval *const &RHS) {
    /**
     * @todo(cscd70) Please finish the implementation of this function that is
     *               used for determining whether one live interval has spill
     *               cost greater than the other.
     */
    return LHS->weight() < RHS->weight();
  }
};

} // namespace std

namespace {

class RAIntfGraph;

class AllocationHints {
private:
  SmallVector<MCPhysReg, 16> Hints;

public:
  AllocationHints(RAIntfGraph *const RA, const LiveInterval *const LI);
  SmallVectorImpl<MCPhysReg>::iterator begin() { return Hints.begin(); }
  SmallVectorImpl<MCPhysReg>::iterator end() { return Hints.end(); }
};

class RAIntfGraph final : public MachineFunctionPass,
                          private LiveRangeEdit::Delegate {  //  用于根据寄存器的存活间隔构建一个图
private:
  MachineFunction *MF;

  SlotIndexes *SI;
  VirtRegMap *VRM;      // 虚拟寄存器到物理寄存器的Map
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  RegisterClassInfo RCI;
  LiveRegMatrix *LRM;
  MachineLoopInfo *MLI;
  LiveIntervals *LIS;       // 从中可以获取寄存器的活跃信息

  /**
   * @brief Interference Graph
   */
  class IntfGraph {     // example中使用了队列的结构来存储LiveInterval结点
  private:
    RAIntfGraph *RA;

    /// Interference Relations
    std::multimap<LiveInterval *, std::unordered_set<Register>,
                  std::greater<LiveInterval *>>     // LiveInterval中本身就包含了一个寄存器对象
        IntfRels;

    /**
     * @brief  Try to materialize all the virtual registers (internal).
     *
     * @return (nullptr, VirtPhysRegMap) in the case when a successful
     *         materialization is made, (LI, *) in the case when unsuccessful
     *         (and LI is the live interval to spill)
     *
     * @sa tryMaterializeAll
     */
    using MaterializeResult_t =
        std::tuple<LiveInterval *,
                   std::unordered_map<LiveInterval *, MCPhysReg>>;
    MaterializeResult_t tryMaterializeAllInternal();

  public:
    explicit IntfGraph(RAIntfGraph *const RA) : RA(RA) {}
    /**
     * @brief Insert a virtual register @c Reg into the interference graph.
     */
    void insert(const Register &Reg);
    /**
     * @brief Erase a virtual register @c Reg from the interference graph.
     *
     * @sa RAIntfGraph::LRE_CanEraseVirtReg
     */
    void erase(const Register &Reg);
    /**
     * @brief Build the whole graph.
     */
    void build();
    /**
     * @brief Try to materialize all the virtual registers.
     */
    void tryMaterializeAll();
    void clear() { IntfRels.clear(); }
  } G;      // 图相关的数据结构

  SmallPtrSet<MachineInstr *, 32> DeadRemats;     // 目标机器的指令集
  std::unique_ptr<Spiller> SpillerInst;     // 被Spiller的指令

  void postOptimization() {
    SpillerInst->postOptimization();
    for (MachineInstr *const DeadInst : DeadRemats) {
      LIS->RemoveMachineInstrFromMaps(*DeadInst);
      DeadInst->eraseFromParent();
    }
    DeadRemats.clear();
    G.clear();
  }

  friend class AllocationHints;
  friend class IntfGraph;

  /// The following two methods are inherited from @c LiveRangeEdit::Delegate
  /// and implicitly used by the spiller to edit the live ranges.
  bool LRE_CanEraseVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and erase it from the interference graph.
    return true;
  }
  void LRE_WillShrinkVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and re-insert it into the interference graph.
  }

public:
  static char ID;

  StringRef getPassName() const override {
    return "Interference Graph Register Allocator";
  }

  RAIntfGraph() : MachineFunctionPass(ID), G(this) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.setPreservesCFG();
#define REQUIRE_AND_PRESERVE_PASS(PassName)                                    \
  AU.addRequired<PassName>();                                                  \
  AU.addPreserved<PassName>()

    REQUIRE_AND_PRESERVE_PASS(SlotIndexes);
    REQUIRE_AND_PRESERVE_PASS(VirtRegMap);
    REQUIRE_AND_PRESERVE_PASS(LiveIntervals);
    REQUIRE_AND_PRESERVE_PASS(LiveRegMatrix);
    REQUIRE_AND_PRESERVE_PASS(LiveStacks);
    REQUIRE_AND_PRESERVE_PASS(AAResultsWrapperPass);
    REQUIRE_AND_PRESERVE_PASS(MachineDominatorTree);
    REQUIRE_AND_PRESERVE_PASS(MachineLoopInfo);
    REQUIRE_AND_PRESERVE_PASS(MachineBlockFrequencyInfo);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
}; // class RAIntfGraph

AllocationHints::AllocationHints(RAIntfGraph *const RA,
                                 const LiveInterval *const LI) {
  const TargetRegisterClass *const RC = RA->MRI->getRegClass(LI->reg());

  /**
   * @todo(cscd70) Please complete this part by constructing the allocation
   *               hints, similar to the tutorial example.
   */

  outs() << "Hint Registers for Class " << RA->TRI->getRegClassName(RC)
         << ": [";
  for (const MCPhysReg &PhysReg : Hints) {
    outs() << RA->TRI->getRegAsmName(PhysReg) << ", ";
  }
  outs() << "]\n";
}

bool RAIntfGraph::runOnMachineFunction(MachineFunction &MF) {
  outs() << "************************************************\n"
         << "* Machine Function\n"
         << "************************************************\n";
  SI = &getAnalysis<SlotIndexes>();
  for (const MachineBasicBlock &MBB : MF) {
    MBB.print(outs(), SI);
    outs() << "\n";
  }
  outs() << "\n\n";

  this->MF = &MF;

  VRM = &getAnalysis<VirtRegMap>();
  TRI = &VRM->getTargetRegInfo();
  MRI = &VRM->getRegInfo();
  MRI->freezeReservedRegs(MF);
  LIS = &getAnalysis<LiveIntervals>();
  LRM = &getAnalysis<LiveRegMatrix>();
  RCI.runOnMachineFunction(MF);
  MLI = &getAnalysis<MachineLoopInfo>();

  SpillerInst.reset(createInlineSpiller(*this, MF, *VRM));

  G.build();        // 构建冲突图
  G.tryMaterializeAll();

  postOptimization();
  return true;
}

void RAIntfGraph::IntfGraph::insert(const Register &Reg) {
  /**
   * @todo(cscd70) Please implement this method.
   */
  // 1. Collect all VIRTUAL registers that interfere with 'Reg'.
  // 2. Collect all PHYSICAL registers that interfere with 'Reg'.
  // 3. Update the weights of Reg (and its interfering neighbors), using the
  //    formula on "Lecture 6 Register Allocation Page 23".
  // 4. Insert 'Reg' into the graph.
  auto inter = &(RA->LIS->getInterval(Reg));
  // 在邻接表中增加这一项
  IntfRels.insert({inter, {}});
  // 将其加入到其他结点的邻居中， 也需要将其他结点加入到新结点的邻居中
  for (size_t i = 0; i < RA->MRI->getNumVirtRegs(); ++i) { // 所有寄存器
      Register tmp_reg = Register::index2VirtReg(i);
      LiveInterval *tmp_liveness = &(RA->LIS->getInterval(Reg));
      if (RA->MRI->reg_nodbg_empty(Reg) || tmp_reg == Reg) {    // 所有已经被使用的先跳过, 什么样的才算是被使用过的呢？
          continue;
      }
      // 需不需要将带有Spill标记的不进行处理呢？
      if (tmp_liveness->overlaps(inter)) { // 判断与Reg对应的liveness是否有交集
            // 将Reg加入到tmp对应的邻居中
            auto tmp_inter_it = IntfRels.find(tmp_liveness);
            if (tmp_inter_it != IntfRels.end()) {
                tmp_inter_it->second.insert(Reg);
            }
            // 将Reg的邻居中加入tmp
            auto reg_inter_it = IntfRels.find(inter);
            if (reg_inter_it != IntfRels.end()) {
                reg_inter_it->second.insert(tmp_reg);
            }
      }
  }




}

void RAIntfGraph::IntfGraph::erase(const Register &Reg) {
  /**
   * @todo(cscd70) Please implement this method.
   */
  // 1. ∀n ∈ neighbors(Reg), erase 'Reg' from n's interfering set and update its
  //    weights accordingly.
  // 2. Erase 'Reg' from the interference graph.
  LiveInterval *reg_live = nullptr;
  for (auto &inter_regs : IntfRels) { // 首先遍历每个Interval, 将其中邻居等于Reg的邻居移除
      auto inter = inter_regs.first;
      if (inter->reg() == Reg) {
          reg_live = inter;
      }
      auto regs = inter_regs.second;
      if (regs.count(Reg)) {  // 如果存在Reg作为其邻居
          regs.erase(Reg);
      }
  }
  // 最后将其中Reg对应的Interval项移除就好了
  if (reg_live != nullptr) {
      IntfRels.erase(reg_live);
  }
}

void RAIntfGraph::IntfGraph::build() {
  /**
   * @todo(cscd70) Please implement this method.
   */
    for (unsigned VirtualRegIdx = 0; VirtualRegIdx < RA->MRI->getNumVirtRegs();
         ++VirtualRegIdx) { // 遍历虚拟寄存器
        Register Reg = Register::index2VirtReg(VirtualRegIdx);    // 通过索引获取寄存器对象
        // skip all unused registers
        if (RA->MRI->reg_nodbg_empty(Reg)) {    // 所有已经被使用的先跳过, 什么样的才算是被使用过的呢？
            continue;
        }
        insert(Reg);      // 加入结点
    }
}

RAIntfGraph::IntfGraph::MaterializeResult_t
RAIntfGraph::IntfGraph::tryMaterializeAllInternal() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;

  /**
   * @todo(cscd70) Please implement this method.
   */
  // ∀r ∈ IntfRels.keys, try to materialize it. If successful, cache it in
  // PhysRegAssignment, else mark it as to be spilled.

  return std::make_tuple(nullptr, PhysRegAssignment);
}

void RAIntfGraph::IntfGraph::tryMaterializeAll() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;

  /**
   * @todo(cscd70) Please implement this method.
   */
  // Keep looping until a valid assignment is made. In the case of spilling,
  // modify the interference graph accordingly.

  for (auto &PhysRegAssignPair : PhysRegAssignment) {
    RA->LRM->assign(*PhysRegAssignPair.first, PhysRegAssignPair.second);
  }
}

char RAIntfGraph::ID = 0;

static RegisterRegAlloc X("intfgraph", "Interference Graph Register Allocator",
                          []() -> FunctionPass * { return new RAIntfGraph(); });

} // anonymous namespace

INITIALIZE_PASS_BEGIN(RAIntfGraph, "regallointfgraph",
                      "Interference Graph Register Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_DEPENDENCY(LiveStacks);
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass);
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree);
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo);
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo);
INITIALIZE_PASS_END(RAIntfGraph, "regallointfgraph",
                    "Interference Graph Register Allocator", false, false)
