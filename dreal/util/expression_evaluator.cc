#include "dreal/util/expression_evaluator.h"

#include <algorithm>  // to suppress cpplint for the use of 'min'
#include <numeric>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/math.h"

namespace dreal {

using std::accumulate;
using std::pair;

ExpressionEvaluator::ExpressionEvaluator(Expression e) : e_{std::move(e)} {}

Box::Interval ExpressionEvaluator::operator()(const Box& box) const {
  return Visit(e_, box);
}

Box::Interval ExpressionEvaluator::Visit(const Expression& e,
                                         const Box& box) const {
  return VisitExpression<Box::Interval>(this, e, box);
}

Box::Interval ExpressionEvaluator::VisitVariable(const Expression& e,
                                                 const Box& box) const {
  const Variable& var{get_variable(e)};
  return box[var];
}

Box::Interval ExpressionEvaluator::VisitConstant(const Expression& e,
                                                 const Box&) const {
  const double c{get_constant_value(e)};
  return Box::Interval{c};
}

Box::Interval ExpressionEvaluator::VisitRealConstant(const Expression& e,
                                                     const Box&) const {
  const double lb{get_lb_of_real_constant(e)};
  const double ub{get_ub_of_real_constant(e)};
  return Box::Interval{lb, ub};
}

Box::Interval ExpressionEvaluator::VisitAddition(const Expression& e,
                                                 const Box& box) const {
  const double c{get_constant_in_addition(e)};
  const auto& expr_to_coeff_map = get_expr_to_coeff_map_in_addition(e);
  return accumulate(expr_to_coeff_map.begin(), expr_to_coeff_map.end(),
                    ibex::Interval{c},
                    [this, &box](const Box::Interval& init,
                                 const pair<const Expression, double>& p) {
                      return init + Visit(p.first, box) * p.second;
                    });
}

Box::Interval ExpressionEvaluator::VisitMultiplication(const Expression& e,
                                                       const Box& box) const {
  const double c{get_constant_in_multiplication(e)};
  const auto& base_to_exponent_map =
      get_base_to_exponent_map_in_multiplication(e);
  return accumulate(base_to_exponent_map.begin(), base_to_exponent_map.end(),
                    ibex::Interval{c},
                    [this, &box](const Box::Interval& init,
                                 const pair<const Expression, Expression>& p) {
                      return init * VisitPow(p.first, p.second, box);
                    });
}

Box::Interval ExpressionEvaluator::VisitDivision(const Expression& e,
                                                 const Box& box) const {
  return Visit(get_first_argument(e), box) / Visit(get_second_argument(e), box);
}

Box::Interval ExpressionEvaluator::VisitLog(const Expression& e,
                                            const Box& box) const {
  return log(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitAbs(const Expression& e,
                                            const Box& box) const {
  return abs(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitExp(const Expression& e,
                                            const Box& box) const {
  return exp(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitSqrt(const Expression& e,
                                             const Box& box) const {
  return sqrt(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitPow(const Expression& e,
                                            const Box& box) const {
  return VisitPow(get_first_argument(e), get_second_argument(e), box);
}

Box::Interval ExpressionEvaluator::VisitPow(const Expression& e1,
                                            const Expression& e2,
                                            const Box& box) const {
  const Box::Interval first{Visit(e1, box)};
  const Box::Interval second{Visit(e2, box)};
  if (second.is_degenerated() && !second.is_empty()) {
    // This indicates that this interval is a point.
    DREAL_ASSERT(second.lb() == second.ub());
    const double point{second.lb()};
    if (is_integer(point)) {
      if (point == 2.0) {
        return sqr(first);
      } else {
        return pow(first, static_cast<int>(point));
      }
    } else {
      return pow(first, point);
    }
  } else {
    return pow(first, second);
  }
}

Box::Interval ExpressionEvaluator::VisitSin(const Expression& e,
                                            const Box& box) const {
  return sin(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitCos(const Expression& e,
                                            const Box& box) const {
  return cos(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitTan(const Expression& e,
                                            const Box& box) const {
  return tan(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitAsin(const Expression& e,
                                             const Box& box) const {
  return asin(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitAcos(const Expression& e,
                                             const Box& box) const {
  return acos(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitAtan(const Expression& e,
                                             const Box& box) const {
  return atan(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitAtan2(const Expression& e,
                                              const Box& box) const {
  return atan2(Visit(get_first_argument(e), box),
               Visit(get_second_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitSinh(const Expression& e,
                                             const Box& box) const {
  return sinh(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitCosh(const Expression& e,
                                             const Box& box) const {
  return cosh(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitTanh(const Expression& e,
                                             const Box& box) const {
  return tanh(Visit(get_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitMin(const Expression& e,
                                            const Box& box) const {
  return min(Visit(get_first_argument(e), box),
             Visit(get_second_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitMax(const Expression& e,
                                            const Box& box) const {
  return max(Visit(get_first_argument(e), box),
             Visit(get_second_argument(e), box));
}

Box::Interval ExpressionEvaluator::VisitIfThenElse(const Expression&,
                                                   const Box&) const {
  throw DREAL_RUNTIME_ERROR("If-then-else expression is not supported yet.");
}

Box::Interval ExpressionEvaluator::VisitUninterpretedFunction(
    const Expression&, const Box&) const {
  throw DREAL_RUNTIME_ERROR("Uninterpreted function is not supported.");
}

std::ostream& operator<<(std::ostream& os,
                         const ExpressionEvaluator& expression_evaluator) {
  return os << "ExpressionEvaluator(" << expression_evaluator.e_ << ")";
}

namespace {

// Returns the center point of the input box @p b.
Box Mid(const Box& b) {
  Box ret{b};
  for (int i = 0; i < b.size(); ++i) {
    ret[i] = ret[i].mid();
  }
  return ret;
}

// Returns the center point of the input box @p b.
// Box LB(const Box& b) {
//   Box ret{b};
//   for (int i = 0; i < b.size(); ++i) {
//     ret[i] = ret[i].lb();
//   }
//   return ret;
// }

// Box UB(const Box& b) {
//   Box ret{b};
//   for (int i = 0; i < b.size(); ++i) {
//     ret[i] = ret[i].ub();
//   }
//   return ret;
// }

}  // namespace

Box::Interval Eval(const Expression& f, const Box& x) {
  return ExpressionEvaluator(f)(x);
}

Box::Interval Taylor1Eval(const Expression& f, const Box& x) {
  // Taylor₁(f)([x]) = f(x⁰) + ∑ᵢ ([∂f/∂xᵢ]([x]) * ([xᵢ] - x⁰ᵢ))

  // Step 1. Pick a point x0 in [x]. For now, we pick the mid point.
  const Box x0{Mid(x)};

  // Step 2. Compute f(x⁰).
  Box::Interval ret{Eval(f, x0)};

  // Step 3. Compute the sum part.
  for (int i = 0; i < x.size(); ++i) {
    ret +=
        ExpressionEvaluator(f.Differentiate(x.variable(i)))(x) * (x[i] - x0[i]);
  }
  return ret;
}

Box::Interval Taylor2Eval(const Expression& f, const Box& x) {
  // Taylor₂(f)([x]) = f(x⁰) + ∑ᵢ ([∂f/∂xᵢ]([x]) * ([xᵢ] - x⁰ᵢ))
  //                 + 1/2 ∑ᵢ∑ⱼ ([∂²f/∂xᵢⱼ]([x]) * ([xᵢ] - x⁰ᵢ) * ([xⱼ] - x⁰ⱼ))
  // Step 1. Pick a point x0 in [x]. For now, we pick the mid point.
  const Box x0{Mid(x)};

  // Step 2. Compute f(x⁰).
  Box::Interval ret{Eval(f, x0)};

  // Step 3. Compute the first-order part.
  for (int i = 0; i < x.size(); ++i) {
    ret += ExpressionEvaluator(f.Differentiate(x.variable(i)))(x0) *
           (x[i] - x0[i]);
  }

  // Step 3. Compute the first-order part.
  for (int i = 0; i < x.size(); ++i) {
    for (int j = i; j < x.size(); ++j) {
      if (i == j) {
        ret += 0.5 *
               ExpressionEvaluator(
                   f.Differentiate(x.variable(i)).Differentiate(x.variable(j)))(
                   x) *
               (x[i] - x0[i]) * (x[j] - x0[j]);
      } else {
        ret += ExpressionEvaluator(
                   f.Differentiate(x.variable(i)).Differentiate(x.variable(j)))(
                   x) *
               (x[i] - x0[i]) * (x[j] - x0[j]);
      }
    }
  }
  return ret;
}

}  // namespace dreal