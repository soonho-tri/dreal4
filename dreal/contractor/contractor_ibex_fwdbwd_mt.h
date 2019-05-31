#pragma once

#include <memory>
#include <ostream>
#include <thread>

#include <cuckoohash_map.hh>

#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {

/// Multi-thread version of ContractorIbexFwdbwd contractor.
class ContractorIbexFwdbwdMt : public ContractorCell {
 public:
  /// Deleted default constructor.
  ContractorIbexFwdbwdMt() = delete;

  /// Constructs IbexFwdbwdMt contractor using @p f and @p box.
  ContractorIbexFwdbwdMt(Formula f, const Box& box, const Config& config);

  /// Deleted copy constructor.
  ContractorIbexFwdbwdMt(const ContractorIbexFwdbwdMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexFwdbwdMt(ContractorIbexFwdbwdMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexFwdbwdMt& operator=(const ContractorIbexFwdbwdMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexFwdbwdMt& operator=(ContractorIbexFwdbwdMt&&) = delete;

  ~ContractorIbexFwdbwdMt() override = default;

  void Prune(ContractorStatus* cs) const override;

  std::ostream& display(std::ostream& os) const override;

 private:
  ContractorIbexFwdbwd* GetCtcOrCreate(const Box& box) const;

  const Formula f_;
  const Config config_;

  mutable std::vector<int> ctc_ready_;
  mutable std::vector<std::unique_ptr<ContractorIbexFwdbwd>> ctcs_;
};

}  // namespace dreal