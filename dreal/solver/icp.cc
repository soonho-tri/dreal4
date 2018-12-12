#include "dreal/solver/icp.h"

#include <tuple>
#include <utility>

#include "dreal/solver/branch_gradient_descent.h"
#include "dreal/solver/branch_max_diam.h"
#include "dreal/util/logging.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

using std::cout;
using std::pair;
using std::tie;
using std::vector;
using std::experimental::nullopt;
using std::experimental::optional;

namespace dreal {

namespace {

// A class to show statistics information at destruction. We have a
// static instance in Icp::CheckSat() to keep track of the numbers of
// branching and pruning operations.
class IcpStat : public Stat {
 public:
  explicit IcpStat(const bool enabled) : Stat{enabled} {}
  IcpStat(const IcpStat&) = default;
  IcpStat(IcpStat&&) = default;
  IcpStat& operator=(const IcpStat&) = default;
  IcpStat& operator=(IcpStat&&) = default;
  ~IcpStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of Branching",
            "ICP level", num_branch_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of Pruning",
            "ICP level", num_prune_);
      if (num_branch_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Branching", "ICP level",
              timer_branch_.seconds());
      }
      if (num_prune_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning", "ICP level",
              timer_prune_.seconds());
      }
      print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
            "Total time spent in Evaluation", "ICP level",
            timer_eval_.seconds());
    }
  }

  int num_branch_{0};
  int num_prune_{0};
  Timer timer_branch_;
  Timer timer_prune_;
  Timer timer_eval_;
};

}  // namespace

Icp::Icp(const Config& config) : config_{config} {}

// Evaluates @p box using @p formula_evaluators.
//
// It evaluates each formula with @p box using interval
// arithmetic. There are three possible outcomes:
//
//  - UNSAT: It shows that there is no solution in the box. The
//           function immediately returns `nullopt`.

//  - VALID: It shows that all the points in the box satisfy the
//           constraint.
//
//  - UNKNOWN: It cannot conclude if the constraint is satisfied or
//             not completely. It checks the width/diameter of the
//             interval evaluation and adds the free variables in the
//             constraint into the set that it will return.
//
// If it returns an ibex::BitSet, it represents the dimensions on
// which the ICP algorithm needs to consider branching.
optional<ibex::BitSet> Icp::EvaluateBox(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    ContractorStatus* const cs) {
  ibex::BitSet branching_candidates(box.size());  // This function returns this.
  for (const FormulaEvaluator& formula_evaluator : formula_evaluators) {
    const FormulaEvaluationResult result{formula_evaluator(box)};
    switch (result.type()) {
      case FormulaEvaluationResult::Type::UNSAT:
        DREAL_LOG_DEBUG(
            "Icp::EvaluateBox() Found that the box\n"
            "{0}\n"
            "has no solution for {1} (evaluation = {2}).",
            box, formula_evaluator, result.evaluation());
        cs->mutable_box().set_empty();
        cs->AddUsedConstraint(formula_evaluator.formula());
        return nullopt;
      case FormulaEvaluationResult::Type::VALID:
        DREAL_LOG_DEBUG(
            "Icp::EvaluateBox() Found that all points in the box\n"
            "{0}\n"
            "satisfies the constraint {1} (evaluation = {2}).",
            box, formula_evaluator, result.evaluation());
        continue;
      case FormulaEvaluationResult::Type::UNKNOWN: {
        const Box::Interval& evaluation{result.evaluation()};
        const double diam{evaluation.diam()};
        if (diam > config_.precision()) {
          DREAL_LOG_DEBUG(
              "Icp::EvaluateBox() Found an interval >= precision({2}):\n"
              "{0} -> {1}",
              formula_evaluator, evaluation, config_.precision());
          for (const Variable& v : formula_evaluator.variables()) {
            if (box[v].is_bisectable()) {
              branching_candidates.add(box.index(v));
            }
          }
        }
        break;
      }
    }
  }
  return branching_candidates;
}

bool Icp::CheckSat(const Contractor& contractor,
                   const vector<FormulaEvaluator>& formula_evaluators,
                   ContractorStatus* const cs) {
  // Use the stacking policy set by the configuration.
  stack_left_box_first_ = config_.stack_left_box_first();
  static IcpStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_LOG_DEBUG("Icp::CheckSat()");
  // Stack of Box x BranchingPoint.
  vector<pair<Box, int>> stack;
  stack.emplace_back(
      cs->box(),
      // -1 indicates that the very first box does not come from a branching.
      -1);

  // `current_box` always points to the box in the contractor status
  // as a mutable reference.
  Box& current_box{cs->mutable_box()};
  // `current_branching_point` always points to the branching_point in
  // the contractor status as a mutable reference.
  int& current_branching_point{cs->mutable_branching_point()};

  TimerGuard prune_timer_guard(&stat.timer_prune_, stat.enabled(),
                               false /* start_timer */);
  TimerGuard eval_timer_guard(&stat.timer_eval_, stat.enabled(),
                              false /* start_timer */);
  TimerGuard branch_timer_guard(&stat.timer_branch_, stat.enabled(),
                                false /* start_timer */);

  VectorX<Expression> constraints(static_cast<int>(formula_evaluators.size()));
  if (config_.branching_strategy() ==
      Config::BranchingStrategy::GradientDescent) {
    for (size_t i{0}; i < formula_evaluators.size(); ++i) {
      const FormulaEvaluator& formula_evaluator{formula_evaluators[i]};
      constraints[i] = ToErrorFunction(formula_evaluator.formula());
    }
  }

  while (!stack.empty()) {
    DREAL_LOG_DEBUG("Icp::CheckSat() Loop Head");
    // 1. Pop the current box from the stack
    tie(current_box, current_branching_point) = stack.back();
    stack.pop_back();

    // 2. Prune the current box.
    DREAL_LOG_TRACE("Icp::CheckSat() Current Box:\n{}", current_box);
    prune_timer_guard.resume();
    contractor.Prune(cs);
    prune_timer_guard.pause();
    stat.num_prune_++;
    DREAL_LOG_TRACE("Icp::CheckSat() After pruning, the current box =\n{}",
                    current_box);

    if (current_box.empty()) {
      // 3.1. The box is empty after pruning.
      DREAL_LOG_DEBUG("Icp::CheckSat() Box is empty after pruning");
      continue;
    }
    // 3.2. The box is non-empty. Check if the box is still feasible
    // under evaluation and it's small enough.
    eval_timer_guard.resume();
    const optional<ibex::BitSet> branching_candidates{
        EvaluateBox(formula_evaluators, current_box, cs)};
    if (!branching_candidates) {
      // 3.2.1. We detect that the current box is not a feasible solution.
      DREAL_LOG_DEBUG(
          "Icp::CheckSat() Detect that the current box is not feasible by "
          "evaluation:\n{}",
          current_box);
      continue;
    }
    if (branching_candidates->empty()) {
      // 3.2.2. delta-SAT : We find a box which is smaller enough.
      DREAL_LOG_DEBUG("Icp::CheckSat() Found a delta-box:\n{}", current_box);
      return true;
    }
    eval_timer_guard.pause();

    // 3.2.3. This box is bigger than delta. Need branching.
    branch_timer_guard.resume();
    stat.num_branch_++;
    switch (config_.branching_strategy()) {
      case Config::BranchingStrategy::MaxDiam:
        if (BranchMaxDiam(current_box, *branching_candidates,
                          stack_left_box_first_, &stack)) {
          return true;
        }
        // We alternate between adding-the-left-box-first policy and
        // adding-the-right-box-first policy.
        stack_left_box_first_ = !stack_left_box_first_;
        break;
      case Config::BranchingStrategy::GradientDescent:
        if (BranchGradientDescent(constraints, config_, *branching_candidates,
                                  &current_box, &stack)) {
          return true;
        }
        break;
    }
    branch_timer_guard.pause();
  }
  DREAL_LOG_DEBUG("Icp::CheckSat() No solution");
  return false;
}
}  // namespace dreal
