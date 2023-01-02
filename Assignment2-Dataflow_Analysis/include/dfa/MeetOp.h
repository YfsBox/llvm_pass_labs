#pragma once // NOLINT(llvm-header-guard)

#include <vector>

namespace dfa {

template <typename TDomainElemRepr> //
class MeetOp {
protected:
  using DomainVal_t = std::vector<TDomainElemRepr>;

public:
  virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                 const DomainVal_t &RHS) const = 0;
  virtual DomainVal_t top(const size_t DomainSize) const = 0;
};

/**
 * @brief Intersection Meet Operator
 *
 * @todo(cscd70) Please complete the definition of the intersection meet
 *               operator, and modify the existing definition if necessary.
 */
class Intersect final : public MeetOp<bool> {
public:
  virtual DomainVal_t operator()(const DomainVal_t &LHS,
                                 const DomainVal_t &RHS) const override {
    auto domainval_size = LHS.size();
    DomainVal_t result_domainval;
    result_domainval.resize(domainval_size);
    for (unsigned int i = 0; i < domainval_size; i++) {
        result_domainval[i] = (LHS[i] && RHS[i]);
    }
    return result_domainval;
  }
  virtual DomainVal_t top(const size_t DomainSize) const override { // 一个全是0的集合

    return DomainVal_t(DomainSize, false);
  }
};

} // namespace dfa
