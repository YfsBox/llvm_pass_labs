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
#include <stack>
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
  VirtRegMap *VRM;      // 虚拟寄存器到物理寄存器的Map, 也就是我们最终想要的结果
  const TargetRegisterInfo *TRI;    // 目标平台机相关的信息, 比如说目标机寄存器的name等等
  MachineRegisterInfo *MRI;         // 可以理解为虚拟寄存器相关的信息集合
  RegisterClassInfo RCI;
  LiveRegMatrix *LRM;
  MachineLoopInfo *MLI;             // 之前通过数据流分析所得的循环信息，在这里的作用就是获取循环深度用来计算Weight
  LiveIntervals *LIS;       // 之前所收集的, 从中可以获取寄存器的活跃信息
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
    std::map<Register, double> RegsWeightMap;

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
    MCRegister selectOrSplit(LiveInterval *const interval, SmallVectorImpl<Register> *const split_regs);    // 第二个参数用来返回split的结果
    MCRegister tryGetColor(const LiveInterval *interval);
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
    void updateWeight();
    /**
     * @brief Build the whole graph.
     */
    void build();
    /**
     * @brief Try to materialize all the virtual registers.
     */
    void tryMaterializeAll();

    void simplify();

    void clear() {
        IntfRels.clear();
        RegsWeightMap.clear();
    }
  } G;      // 图相关的数据结构

  SmallPtrSet<MachineInstr *, 32> DeadRemats;     // 目标机器的指令集
  std::unique_ptr<Spiller> SpillerInst;
  std::set<Register> SpillerRegSet;         // 被Spill的集合
  std::map<Register, bool> IsOnStack;       // 用于运行算法时的栈
  std::stack<LiveInterval*> RuningStack;

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
  bool LRE_CanEraseVirtReg(Register Reg) override {     // 需要被spiller用来修改live range
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

  // G.build();        // 构建冲突图
  G.tryMaterializeAll();       // try目前的实现中只要走一遍算法就可以

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
  // 将其加入到其他结点的邻居中， 也需要将其他结点加入到新结点的邻居中, 这一部分只对virtual部分进行了处理
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

void RAIntfGraph::IntfGraph::updateWeight() {       // 这一部分算是直接对ppt上公式的实现
    // 更新Weight map, 这个map指的是Spill对应的Weight
    for (auto &inter_map: IntfRels) {
        LiveInterval *interval = inter_map.first;
        Register reg = interval->reg();
        double new_spill_weight = 0;
        // 通过虚拟寄存器的def-use来计算出来Weight
        for (auto reg_inst_it = RA->MRI->reg_instr_begin(reg);
                    reg_inst_it != RA->MRI->reg_instr_end(); ++reg_inst_it) {
            MachineInstr *McInst = &(*reg_inst_it);
            unsigned loop_depth = RA->MLI->getLoopDepth(McInst->getParent());
            auto read_write = McInst->readsWritesVirtualRegister(reg);  // 得出当前该指令对外层循环中的reg的读写情况
            new_spill_weight += (read_write.first + read_write.second) * pow(10, loop_depth);
        }
        auto degree_cnt = static_cast<double>(IntfRels.find(interval)->second.size());
        RegsWeightMap[reg] = new_spill_weight / degree_cnt;
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
        if (RA->MRI->reg_nodbg_empty(Reg) || RA->SpillerRegSet.count(Reg)) {    // 所有已经被使用的先跳过, 什么样的才算是被使用过的呢？
            continue;
        }
        RA->IsOnStack[Reg] = false;  // 初始化为不处于栈中
        insert(Reg);      // 加入结点
    }
}

RAIntfGraph::IntfGraph::MaterializeResult_t
RAIntfGraph::IntfGraph::tryMaterializeAllInternal() {   // 这个地方代表的是单轮循环
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;
  /**
   * @todo(cscd70) Please implement this method.
   */
  // ∀r ∈ IntfRels.keys, try to materialize it. If successful, cache it in
  // PhysRegAssignment, else mark it as to be spilled.
  // 也就是seletc操作
  LiveInterval *rt_interval = nullptr;
  while (!RA->RuningStack.empty()) {
      LiveInterval *interval = RA->RuningStack.top();
      RA->RuningStack.pop();
      // 弹出当前结点, 之后尝试对当前结点填充颜色
      RA->LRM->invalidateVirtRegs();    // 每次循环都有可能涉及到对Liveness相关的修改，当修改之后需要请空其缓存
      SmallVector<Register, 4> split_vir_regs;

      MCRegister PhysReg = selectOrSplit(interval, &split_vir_regs);       // Split也就是需要溢出的,这种情况下,需要将其进行split
      if (PhysReg) {  // 如果是可以分配的寄存器,就可以进行赋值了,寄存器分配的目标就在于设置一个Map
          RA->LRM->assign(*interval, PhysReg);  // 对于成功分配了颜色的直接修改好对应的map就行了
      } else {
          rt_interval = interval;
      }
      // enqueue the splitted live ranges
      for (Register Reg : split_vir_regs) {      // 所返回的
          LiveInterval *LI = &RA->LIS->getInterval(Reg);
          if (RA->MRI->reg_nodbg_empty(LI->reg())) {
              RA->LIS->removeInterval(LI->reg());       // 对于分裂的如何进行处理??为什么需要进行分裂呢?
              continue;
          }
          insert(Reg);
      } // 对于Split的应该进行怎样的处理呢?
  }
  return std::make_tuple(rt_interval, PhysRegAssignment);
}

void RAIntfGraph::IntfGraph::tryMaterializeAll() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;
  /**
   * @todo(cscd70) Please implement this method.
   */
  // Keep looping until a valid assignment is made. In the case of spilling,
  // modify the interference graph accordingly.
  using Vir2PhysAssignmentMap = std::unordered_map<LiveInterval *, MCPhysReg>;
  bool spill_finish = false;
  size_t round_cnt = 0;
  while (!spill_finish && round_cnt < 10) {
      dbgs() << "#Begin round " << round_cnt << "\n";
      build(); // 构建冲突图
      updateWeight();  // 更新溢出cost
      simplify();  // 开始简化的过程
      auto select_tuple = tryMaterializeAllInternal();
      auto liveinterval = std::get<LiveInterval*>(select_tuple);             // select
      if (liveinterval == nullptr) {    // 不需要spill的情况
          spill_finish = true;
          PhysRegAssignment = std::get<Vir2PhysAssignmentMap>(select_tuple);
      }
      round_cnt++;
  }

  for (auto &PhysRegAssignPair : PhysRegAssignment) {       // 此时完成了寄存器分配,之后进行赋值
    RA->LRM->assign(*PhysRegAssignPair.first, PhysRegAssignPair.second);
  }
}

void RAIntfGraph::IntfGraph::simplify() {
    // 当整个graph都为空时, 直接返回
    if (IntfRels.empty()) {
        return;
    }
    LiveInterval *select_reg_inter = nullptr; // 被选中的寄存器
    double min_weight = -1;
    // 首先需要遍历一遍graph,从中选出来一个, 优先找出来Weight小的
    for (auto &inter_maps : IntfRels) {
        LiveInterval *inter = inter_maps.first;
        Register reg = inter->reg();
        // 首先判断此结点, 是否已经处于栈中
        if (RA->IsOnStack[reg]) {
            continue;
        }
        if (min_weight == -1 || RegsWeightMap[reg] < min_weight) {
            select_reg_inter = inter;
            min_weight = RegsWeightMap[reg];
        }
    }
    if (min_weight == -1) {     // 表示的没有选出来
        return;
    }
    // 然后压入栈中
    Register select_reg = select_reg_inter->reg();
    RA->IsOnStack[select_reg] = true;
    RA->RuningStack.push(select_reg_inter);
    // 之后调节graph, 从graph中移除该结点
    IntfRels.erase(select_reg_inter);
    simplify(); // 递归地调用
}

MCRegister RAIntfGraph::IntfGraph::selectOrSplit(LiveInterval *const interval,
                                                 SmallVectorImpl<Register> *const split_regs) {
    ArrayRef<MCPhysReg> Order =
            RA->RCI.getOrder(RA->MF->getRegInfo().getRegClass(interval->reg()));  // 获取一个分配顺序
    SmallVector<MCPhysReg, 16> Hints;
    bool IsHardHint = RA->TRI->getRegAllocationHints(interval->reg(), Order, Hints, *RA->MF,
                                                     RA->VRM, RA->LRM);
    if (!IsHardHint) {
        for (auto mcphysreg: Order) {
            Hints.push_back(mcphysreg);
        }
    }
    // 输出一下可以供分配的寄存器的name
    outs() << "Hint Registers: [";
    for (const MCPhysReg &PhysReg : Hints) {
        outs() << RA->TRI->getRegAsmName(PhysReg) << ", ";
    }
    outs() << "]\n";

    SmallVector<MCRegister, 8> PhysRegSpillCandidates;
    // 遍历可以用来分配的物理寄存器,检查该物理寄存器和LI是否是否时刻分配的
    for (auto PhysReg : Hints) {
        switch (RA->LRM->checkInterference(*interval, PhysReg)) {
            case LiveRegMatrix::IK_Free:      // 可以用来分配
                outs() << "Allocating physical register " << RA->TRI->getRegAsmName(PhysReg)
                       << "\n";
                return PhysReg;     // 直接返回就好了
            case LiveRegMatrix::IK_VirtReg:     // 表示该颜色不能分配
                PhysRegSpillCandidates.push_back(PhysReg);  // 先加入到PhyRegSpillCandidates列表,该列表的作用是什么呢???
                continue;
            default:
                continue;
        }
    }
    // 主要问题在于这一部分操作有一些搞不懂
    /*for (MCRegister PhysReg : PhysRegSpillCandidates) {             // 这一部分考虑进行split的操作
        if (!spillInterferences(interval, PhysReg, split_regs)) {            // 会讲split的结果返回到SplitVirtual中
            continue;
        }
        return PhysReg;
    }*/
    LiveRangeEdit LRE(interval, *split_regs, *RA->MF, *RA->LIS, RA->VRM,
                      RA, &RA->DeadRemats);       // 用于修改Liveness范围的
    RA->SpillerInst->spill(LRE);
    return 0;

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
