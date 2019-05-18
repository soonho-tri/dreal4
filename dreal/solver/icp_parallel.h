#pragma once

#include <vector>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/solver/formula_evaluator.h"
#include "dreal/solver/icp.h"

namespace dreal {

/// Class for ICP (Interval Constraint Propagation) algorithm.
class IcpParallel : public Icp {
 public:
  /// Constructs an IcpParallel based on @p config.
  explicit IcpParallel(const Config& config);

  bool CheckSat(const Contractor& contractor,
                const std::vector<FormulaEvaluator>& formula_evaluators,
                ContractorStatus* cs) override;
};

}  // namespace dreal
