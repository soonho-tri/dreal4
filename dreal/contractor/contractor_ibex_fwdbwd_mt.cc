#include "dreal/contractor/contractor_ibex_fwdbwd_mt.h"

#include <utility>

#include "ThreadPool/ThreadPool.h"

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/timer.h"  // TODO(soonho): remove this

using std::make_unique;
using std::ostream;
using std::thread;
using std::unique_ptr;

namespace dreal {

ContractorIbexFwdbwdMt::ContractorIbexFwdbwdMt(Formula f, const Box& box,
                                               const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_FWDBWD,
                     ibex::BitSet::empty(box.size()), config},
      f_{std::move(f)},
      config_{config},
      ctcs_(config_.number_of_jobs()) {
  DREAL_LOG_DEBUG("ContractorIbexFwdbwdMt::ContractorIbexFwdbwdMt");

  for (size_t i = 0; i < ctcs_.size(); ++i) {
    auto ctc_unique_ptr = make_unique<ContractorIbexFwdbwd>(f_, box, config_);
    ContractorIbexFwdbwd* ctc{ctc_unique_ptr.get()};
    DREAL_ASSERT(ctc);
    ctcs_[i] = std::move(ctc_unique_ptr);
  }

  // Build input.
  mutable_input() = ctcs_[0]->input();

  is_dummy_ = ctcs_[0]->is_dummy();
}

ContractorIbexFwdbwd* ContractorIbexFwdbwdMt::GetCtc() const {
  thread_local const int tid{ThreadPool::get_thread_id()};
  return ctcs_[tid].get();
}

void ContractorIbexFwdbwdMt::Prune(ContractorStatus* cs) const {
  DREAL_ASSERT(!is_dummy_);
  ContractorIbexFwdbwd* const ctc{GetCtc()};
  DREAL_ASSERT(ctc);
  return ctc->Prune(cs);
}

ostream& ContractorIbexFwdbwdMt::display(ostream& os) const {
  return os << "IbexFwdbwd(" << f_ << ")";
}

bool ContractorIbexFwdbwdMt::is_dummy() const { return is_dummy_; }

}  // namespace dreal
