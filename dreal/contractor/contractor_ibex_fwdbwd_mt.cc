#include "dreal/contractor/contractor_ibex_fwdbwd_mt.h"

#include <utility>

#include "ThreadPool/ThreadPool.h"

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/timer.h"  // TODO(soonho): remove this

using std::make_unique;
using std::ostream;
using std::unique_ptr;

namespace dreal {

ContractorIbexFwdbwdMt::ContractorIbexFwdbwdMt(Formula f, const Box& box,
                                               const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_FWDBWD, config},
      f_{std::move(f)},
      config_{config},
      ctc_ready_(config_.number_of_jobs(), 0),
      ctcs_(ctc_ready_.size()) {
  DREAL_LOG_DEBUG("ContractorIbexFwdbwdMt::ContractorIbexFwdbwdMt");
}

const ibex::BitSet& ContractorIbexFwdbwdMt::input() const {
  DREAL_ASSERT(ctc_ready_[0]);
  return ctcs_[0]->input();
}

ibex::BitSet& ContractorIbexFwdbwdMt::mutable_input() {
  DREAL_ASSERT(ctc_ready_[0]);
  return ctcs_[0]->mutable_input();
}

ContractorIbexFwdbwd* ContractorIbexFwdbwdMt::GetCtcOrCreate(
    const Box& box) const {
  thread_local const int tid{ThreadPool::get_thread_id()};
  if (ctc_ready_[tid]) {
    return ctcs_[tid].get();
  }
  auto ctc_unique_ptr = make_unique<ContractorIbexFwdbwd>(f_, box, config_);
  ContractorIbexFwdbwd* ctc{ctc_unique_ptr.get()};
  DREAL_ASSERT(ctc);
  ctcs_[tid] = std::move(ctc_unique_ptr);
  ctc_ready_[tid] = 1;
  return ctc;
}

void ContractorIbexFwdbwdMt::Prune(ContractorStatus* cs) const {
  DREAL_ASSERT(!is_dummy());
  ContractorIbexFwdbwd* const ctc{GetCtcOrCreate(cs->box())};
  DREAL_ASSERT(ctc);
  return ctc->Prune(cs);
}

ostream& ContractorIbexFwdbwdMt::display(ostream& os) const {
  return os << "IbexFwdbwd(" << f_ << ")";
}

bool ContractorIbexFwdbwdMt::is_dummy() const {
  DREAL_ASSERT(ctc_ready_[0]);
  return ctcs_[0]->is_dummy();
}

}  // namespace dreal
