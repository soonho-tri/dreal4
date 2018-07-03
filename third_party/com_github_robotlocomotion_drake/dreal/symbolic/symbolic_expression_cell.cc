#include "dreal/symbolic/symbolic_expression_cell.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "dreal/symbolic/hash.h"
#include "dreal/symbolic/symbolic_environment.h"
#include "dreal/symbolic/symbolic_expression.h"
#include "dreal/symbolic/symbolic_expression_visitor.h"
#include "dreal/symbolic/symbolic_variable.h"
#include "dreal/symbolic/symbolic_variables.h"

namespace dreal {
namespace drake {
namespace symbolic {

using std::accumulate;
using std::all_of;
using std::domain_error;
using std::endl;
using std::equal;
using std::hash;
using std::lexicographical_compare;
using std::map;
using std::numeric_limits;
using std::ostream;
using std::ostringstream;
using std::pair;
using std::runtime_error;
using std::setprecision;
using std::string;

namespace {
bool is_integer(const double v) {
  // v should be in [int_min, int_max].
  if (!((numeric_limits<int>::lowest() <= v) &&
        (v <= numeric_limits<int>::max()))) {
    return false;
  }

  double intpart;  // dummy variable
  return modf(v, &intpart) == 0.0;
}

bool is_non_negative_integer(const double v) {
  return (v >= 0) && is_integer(v);
}

// Determines if pow(base, exponent) is polynomial-convertible or not. This
// function is used in constructor of ExpressionPow.
bool determine_polynomial(const Expression& base, const Expression& exponent) {
  // base ^ exponent is polynomial-convertible if the followings hold:
  //    - base is polynomial-convertible.
  //    - exponent is a non-negative integer.
  if (!(base.is_polynomial() && is_constant(exponent))) {
    return false;
  }
  const double e{get_constant_value(exponent)};
  return is_non_negative_integer(e);
}

Expression ExpandMultiplication(const Expression& e1, const Expression& e2,
                                const Expression& e3);

// Helper function expanding (e1 * e2). It assumes that both of e1 and e2 are
// already expanded.
Expression ExpandMultiplication(const Expression& e1, const Expression& e2) {
  // Precondition: e1 and e2 are already expanded.
  assert(e1.EqualTo(e1.Expand()));
  assert(e2.EqualTo(e2.Expand()));

  if (is_addition(e1)) {
    const Expression& e11{get_first_argument(e1)};
    const Expression& e12{get_second_argument(e1)};
    if (is_addition(e2)) {
      const Expression& e21{get_first_argument(e2)};
      const Expression& e22{get_second_argument(e2)};
      //   (e11 + e12) * (e21 + e22)
      // = e11*e21 + e12*e21 + e11*e22 + e12*e22
      return e11 * e21 + e12 * e21 + e11 * e22 + e12 * e22;
    } else {
      //   (e11 + e12) * e2
      // = e11 * e2 + e12 * e2
      return e11 * e2 + e12 * e2;
    }
  } else if (is_addition(e2)) {
    const Expression& e21{get_first_argument(e2)};
    const Expression& e22{get_second_argument(e2)};
    //   e1 * (e21 + e22)
    // = e1 * e21 + e1 * e22;
    return e1 * e21 + e1 * e22;
  }
  return e1 * e2;
}

Expression ExpandMultiplication(const Expression& e1, const Expression& e2,
                                const Expression& e3) {
  return ExpandMultiplication(ExpandMultiplication(e1, e2), e3);
}

// Helper function expanding pow(base, n). It assumes that base is expanded.
Expression ExpandPow(const Expression& base, const int n) {
  // Precondition: base is already expanded.
  assert(base.EqualTo(base.Expand()));
  assert(n >= 1);
  if (n == 1) {
    return base;
  }
  const Expression pow_half{ExpandPow(base, n / 2)};
  if (n % 2 == 1) {
    // pow(base, n) = base * pow(base, n / 2) * pow(base, n / 2)
    return ExpandMultiplication(base, pow_half, pow_half);
  }
  // pow(base, n) = pow(base, n / 2) * pow(base, n / 2)
  return ExpandMultiplication(pow_half, pow_half);
}

// Helper function expanding pow(base, exponent). It assumes that both of base
// and exponent are already expanded.
Expression ExpandPow(const Expression& base, const Expression& exponent) {
  // Precondition: base and exponent are already expanded.
  assert(base.EqualTo(base.Expand()));
  assert(exponent.EqualTo(exponent.Expand()));
  // Expand if
  //     1) base is an addition expression and
  //     2) exponent is a positive integer.
  if (!is_addition(base) || !is_constant(exponent)) {
    return pow(base, exponent);
  }
  const double e{get_constant_value(exponent)};
  if (e <= 0 || !is_integer(e)) {
    return pow(base, exponent);
  }
  const int n{static_cast<int>(e)};
  return ExpandPow(base, n);
}
}  // anonymous namespace

ExpressionCell::ExpressionCell(const ExpressionKind k, const size_t hash,
                               const bool is_poly)
    : kind_{k},
      hash_{hash_combine(static_cast<size_t>(kind_), hash)},
      is_polynomial_{is_poly} {}

Expression ExpressionCell::GetExpression() const { return Expression{this}; }

UnaryExpressionCell::UnaryExpressionCell(const ExpressionKind k,
                                         const Expression& e,
                                         const bool is_poly)
    : ExpressionCell{k, e.get_hash(), is_poly}, e_{e} {}

Variables UnaryExpressionCell::GetVariables() const {
  return e_.GetVariables();
}

bool UnaryExpressionCell::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const auto& unary_e = static_cast<const UnaryExpressionCell&>(e);
  return e_.EqualTo(unary_e.e_);
}

bool UnaryExpressionCell::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const auto& unary_e = static_cast<const UnaryExpressionCell&>(e);
  return e_.Less(unary_e.e_);
}

double UnaryExpressionCell::Evaluate(const Environment& env) const {
  const double v{e_.Evaluate(env)};
  return DoEvaluate(v);
}

BinaryExpressionCell::BinaryExpressionCell(const ExpressionKind k,
                                           const Expression& e1,
                                           const Expression& e2,
                                           const bool is_poly)
    : ExpressionCell{k, hash_combine(e1.get_hash(), e2), is_poly},
      e1_{e1},
      e2_{e2} {}

Variables BinaryExpressionCell::GetVariables() const {
  Variables ret{e1_.GetVariables()};
  ret.insert(e2_.GetVariables());
  return ret;
}

bool BinaryExpressionCell::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const auto& binary_e = static_cast<const BinaryExpressionCell&>(e);
  return e1_.EqualTo(binary_e.e1_) && e2_.EqualTo(binary_e.e2_);
}

bool BinaryExpressionCell::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const auto& binary_e = static_cast<const BinaryExpressionCell&>(e);
  if (e1_.Less(binary_e.e1_)) {
    return true;
  }
  if (binary_e.e1_.Less(e1_)) {
    return false;
  }
  // e1_ equals to binary_e.e1_, compare e2_ and binary_e.e2_
  return e2_.Less(binary_e.e2_);
}

double BinaryExpressionCell::Evaluate(const Environment& env) const {
  const double v1{e1_.Evaluate(env)};
  const double v2{e2_.Evaluate(env)};
  return DoEvaluate(v1, v2);
}

ExpressionVar::ExpressionVar(const Variable& v)
    : ExpressionCell{ExpressionKind::Var, hash_value<Variable>{}(v), true},
      var_{v} {
  // Dummy symbolic variable (ID = 0) should not be used in constructing
  // symbolic expressions.
  assert(!var_.is_dummy());
  // Boolean symbolic variable should not be used in constructing symbolic
  // expressions.
  assert(var_.get_type() != Variable::Type::BOOLEAN);
}

Variables ExpressionVar::GetVariables() const { return {get_variable()}; }

bool ExpressionVar::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return var_.equal_to(static_cast<const ExpressionVar&>(e).var_);
}

bool ExpressionVar::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  // Note the below is using the overloaded operator< between ExpressionVar
  // which is based on variable IDs.
  return var_.less(static_cast<const ExpressionVar&>(e).var_);
}

double ExpressionVar::Evaluate(const Environment& env) const {
  Environment::const_iterator const it{env.find(var_)};
  if (it != env.cend()) {
    assert(!std::isnan(it->second));
    return it->second;
  }
  ostringstream oss;
  oss << "The following environment does not have an entry for the "
         "variable "
      << var_ << endl;
  oss << env << endl;
  throw runtime_error(oss.str());
}

Expression ExpressionVar::Expand() const { return GetExpression(); }

Expression ExpressionVar::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const ExpressionSubstitution::const_iterator it{expr_subst.find(var_)};
  if (it != expr_subst.end()) {
    return it->second;
  }
  return GetExpression();
}

Expression ExpressionVar::Differentiate(const Variable& x) const {
  if (x.equal_to(var_)) {
    return Expression::One();
  }
  return Expression::Zero();
}

ostream& ExpressionVar::Display(ostream& os) const { return os << var_; }

ExpressionConstant::ExpressionConstant(const double v)
    : ExpressionCell{ExpressionKind::Constant, hash<double>{}(v), true}, v_{v} {
  assert(!std::isnan(v));
}

Variables ExpressionConstant::GetVariables() const { return Variables{}; }

bool ExpressionConstant::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return v_ == static_cast<const ExpressionConstant&>(e).v_;
}

bool ExpressionConstant::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return v_ < static_cast<const ExpressionConstant&>(e).v_;
}

double ExpressionConstant::Evaluate(const Environment&) const {
  assert(!std::isnan(v_));
  return v_;
}

Expression ExpressionConstant::Expand() const { return GetExpression(); }

Expression ExpressionConstant::Substitute(const ExpressionSubstitution&,
                                          const FormulaSubstitution&) const {
  assert(!std::isnan(v_));
  return GetExpression();
}

Expression ExpressionConstant::Differentiate(const Variable&) const {
  return Expression::Zero();
}

ostream& ExpressionConstant::Display(ostream& os) const {
  ostringstream oss;
  oss << setprecision(numeric_limits<double>::max_digits10) << v_;
  return os << oss.str();
}

ExpressionRealConstant::ExpressionRealConstant(const double lb, const double ub,
                                               bool use_lb_as_representative)
    : ExpressionCell{ExpressionKind::RealConstant, hash<double>{}(lb), true},
      lb_{lb},
      ub_{ub},
      use_lb_as_representative_{use_lb_as_representative} {
  assert(!std::isnan(lb_));
  assert(!std::isnan(ub_));
  assert(lb < ub_);
  assert(std::nextafter(lb, std::numeric_limits<double>::infinity()) == ub);
}

Variables ExpressionRealConstant::GetVariables() const { return Variables{}; }

bool ExpressionRealConstant::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const ExpressionRealConstant& r =
      static_cast<const ExpressionRealConstant&>(e);
  return lb_ == r.lb_ && ub_ == r.ub_ &&
         use_lb_as_representative_ == r.use_lb_as_representative_;
}

bool ExpressionRealConstant::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return get_value() <
         static_cast<const ExpressionRealConstant&>(e).get_value();
}

double ExpressionRealConstant::Evaluate(const Environment&) const {
  return get_value();
}

Expression ExpressionRealConstant::Expand() const { return GetExpression(); }

Expression ExpressionRealConstant::Substitute(
    const ExpressionSubstitution&, const FormulaSubstitution&) const {
  return GetExpression();
}

Expression ExpressionRealConstant::Differentiate(const Variable&) const {
  return Expression::Zero();
}

ostream& ExpressionRealConstant::Display(ostream& os) const {
  ostringstream oss;
  oss << setprecision(numeric_limits<double>::max_digits10) << "[" << lb_
      << ", " << ub_ << "]";
  return os << oss.str();
}

ExpressionNaN::ExpressionNaN()
    : ExpressionCell{ExpressionKind::NaN, 41, false} {
  // ExpressionCell constructor calls hash_combine(ExpressionKind::NaN, 41) to
  // compute the hash of ExpressionNaN. Here 41 does not have any special
  // meaning.
}

Variables ExpressionNaN::GetVariables() const { return Variables{}; }

bool ExpressionNaN::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return true;
}

bool ExpressionNaN::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  return false;
}

double ExpressionNaN::Evaluate(const Environment&) const {
  throw runtime_error("NaN is detected during Symbolic computation.");
}

Expression ExpressionNaN::Expand() const {
  throw runtime_error("NaN is detected during expansion.");
}

Expression ExpressionNaN::Substitute(const ExpressionSubstitution&,
                                     const FormulaSubstitution&) const {
  throw runtime_error("NaN is detected during substitution.");
}

Expression ExpressionNaN::Differentiate(const Variable&) const {
  throw runtime_error("NaN is detected during differentiation.");
}

ostream& ExpressionNaN::Display(ostream& os) const { return os << "NaN"; }

ExpressionAdd::ExpressionAdd(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Add, e1, e2,
                           e1.is_polynomial() && e2.is_polynomial()} {}

Expression ExpressionAdd::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_expanded{arg1.Expand()};
  const Expression arg2_expanded{arg2.Expand()};
  if (!arg1.EqualTo(arg1_expanded) || !arg2.EqualTo(arg2_expanded)) {
    return arg1_expanded + arg2_expanded;
  } else {
    return GetExpression();
  }
}

Expression ExpressionAdd::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return arg1_subst + arg2_subst;
  } else {
    return GetExpression();
  }
}

Expression ExpressionAdd::Differentiate(const Variable& x) const {
  // ∂/∂x (f + g) = ∂/∂x f + ∂/∂x g
  const Expression& f{get_first_argument()};
  const Expression& g{get_second_argument()};
  return f.Differentiate(x) + g.Differentiate(x);
}

ostream& ExpressionAdd::Display(ostream& os) const {
  return os << "(" << get_first_argument() << " + " << get_second_argument()
            << ")";
}

double ExpressionAdd::DoEvaluate(const double v1, const double v2) const {
  return v1 + v2;
}

ExpressionMul::ExpressionMul(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Mul, e1, e2,
                           e1.is_polynomial() && e2.is_polynomial()} {}

Expression ExpressionMul::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  return ExpandMultiplication(arg1.Expand(), arg2.Expand());
}

Expression ExpressionMul::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return arg1_subst * arg2_subst;
  } else {
    return GetExpression();
  }
}

Expression ExpressionMul::Differentiate(const Variable& x) const {
  // ∂/∂x (f * g) = ∂/∂x f * g  + f * ∂/∂x g
  const Expression& f{get_first_argument()};
  const Expression& g{get_second_argument()};
  return f.Differentiate(x) * g + f * g.Differentiate(x);
}

ostream& ExpressionMul::Display(ostream& os) const {
  const Expression& e1{get_first_argument()};
  const Expression& e2{get_second_argument()};
  if (is_constant(e1) && get_constant_value(e1) == -1) {
    return os << "-" << e2;
  }
  if (is_constant(e2) && get_constant_value(e2) == -1) {
    return os << "-" << e1;
  }
  return os << "(" << e1 << " * " << e2 << ")";
}

double ExpressionMul::DoEvaluate(const double v1, const double v2) const {
  return v1 * v2;
}

// Computes ∂/∂x pow(f, g).
Expression DifferentiatePow(const Expression& f, const Expression& g,
                            const Variable& x) {
  if (is_constant(g)) {
    const Expression& n{g};  // alias n = g
    // Special case where exponent is a constant:
    //     ∂/∂x pow(f, n) = n * pow(f, n - 1) * ∂/∂x f
    return n * pow(f, n - 1) * f.Differentiate(x);
  }
  if (is_constant(f)) {
    const Expression& n{f};  // alias n = f
    // Special case where base is a constant:
    //     ∂/∂x pow(n, g) = log(n) * pow(n, g) * ∂/∂x g
    return log(n) * pow(n, g) * g.Differentiate(x);
  }
  // General case:
  //    ∂/∂x pow(f, g)
  // = ∂/∂f pow(f, g) * ∂/∂x f + ∂/∂g pow(f, g) * ∂/∂x g
  // = g * pow(f, g - 1) * ∂/∂x f + log(f) * pow(f, g) * ∂/∂x g
  // = pow(f, g - 1) * (g * ∂/∂x f + log(f) * f * ∂/∂x g)
  return pow(f, g - 1) *
         (g * f.Differentiate(x) + log(f) * f * g.Differentiate(x));
}

ExpressionDiv::ExpressionDiv(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Div, e1, e2,
                           e1.is_polynomial() && is_constant(e2)} {}

namespace {
// Helper class to implement ExpressionDiv::Expand. Given a symbolic expression
// `e` and a constant `n`, it pushes the division in `e / n` inside for the
// following cases:
//
// Case Addition      : e =  (e1 + e2) / n
//                        => e1 / n + e2 / n
//
// Case Multiplication: e =  (e1 * e2) / n
//                        => (e1 / n * e2)
//
// Case Division      : e =  (e₁ / m) / n
//                        => Recursively simplify e₁ / (n * m)
//
//                      e =  (e₁ / e₂) / n
//                        =  (e₁ / n) / e₂
//                        => Recursively simplify (e₁ / n) and divide it by e₂
//
// For other cases, it does not perform any simplifications.
//
// Note that we use VisitExpression instead of VisitPolynomial because we want
// to handle cases such as `(6xy / z) / 3` where (6xy / z) is not a polynomial
// but it's desirable to simplify the expression into `2xy / z`.
class DivExpandVisitor {
 public:
  Expression Simplify(const Expression& e, const double n) const {
    return VisitExpression<Expression>(this, e, n);
  }

 private:
  Expression VisitAddition(const Expression& e, const double n) const {
    // e = (e1 + e2) / n
    //   = e1 / n + e2 / n
    const Expression& e1{get_first_argument(e)};
    const Expression& e2{get_second_argument(e)};
    return e1 / n + e2 / n;
  }
  Expression VisitMultiplication(const Expression& e, const double n) const {
    // e = (e1 * e2) / n
    //   = (e1 / n * e2)
    const Expression& e1{get_first_argument(e)};
    const Expression& e2{get_second_argument(e)};
    return e1 / n * e2;
  }
  Expression VisitDivision(const Expression& e, const double n) const {
    const Expression& e1{get_first_argument(e)};
    const Expression& e2{get_second_argument(e)};
    if (is_constant(e2)) {
      // e =  (e₁ / m) / n
      //   => Simplify `e₁ / (n * m)`
      const double m{get_constant_value(e2)};
      return Simplify(e1, m * n);
    } else {
      // e =  (e₁ / e₂) / n
      //   => (e₁ / n) / e₂
      return Simplify(e1, n) / e2;
    }
  }
  Expression VisitVariable(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitConstant(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitRealConstant(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitLog(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitPow(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitAbs(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitExp(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitSqrt(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitSin(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitCos(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitTan(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitAsin(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitAcos(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitAtan(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitAtan2(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitSinh(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitCosh(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitTanh(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitMin(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitMax(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitIfThenElse(const Expression& e, const double n) const {
    return e / n;
  }
  Expression VisitUninterpretedFunction(const Expression& e,
                                        const double n) const {
    return e / n;
  }

  // Makes VisitExpression a friend of this class so that VisitExpression can
  // use its private methods.
  friend Expression dreal::drake::symbolic::VisitExpression<Expression>(
      const DivExpandVisitor*, const Expression&, const double&);
};
}  // namespace

Expression ExpressionDiv::Expand() const {
  const Expression e1{get_first_argument().Expand()};
  const Expression e2{get_second_argument().Expand()};
  if (is_constant(e2)) {
    // Simplifies the 'division by a constant' case, using DivExpandVisitor
    // defined above.
    return DivExpandVisitor{}.Simplify(e1, get_constant_value(e2));
  } else {
    return GetExpression();
  }
}

Expression ExpressionDiv::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& e1{get_first_argument()};
  const Expression& e2{get_second_argument()};
  const Expression e1_subst{e1.Substitute(expr_subst, formula_subst)};
  const Expression e2_subst{e2.Substitute(expr_subst, formula_subst)};
  if (!e1.EqualTo(e1_subst) || !e2.EqualTo(e2_subst)) {
    // If anything changed, create and return a new one.
    return e1_subst / e2_subst;
  } else {
    // Otherwise, return self.
    return GetExpression();
  }
}

Expression ExpressionDiv::Differentiate(const Variable& x) const {
  // ∂/∂x (f / g) = (∂/∂x f * g - f * ∂/∂x g) / g^2
  const Expression& f{get_first_argument()};
  const Expression& g{get_second_argument()};
  return (f.Differentiate(x) * g - f * g.Differentiate(x)) / pow(g, 2.0);
}

ostream& ExpressionDiv::Display(ostream& os) const {
  return os << "(" << get_first_argument() << " / " << get_second_argument()
            << ")";
}

double ExpressionDiv::DoEvaluate(const double v1, const double v2) const {
  if (v2 == 0.0) {
    ostringstream oss;
    oss << "Division by zero: " << v1 << " / " << v2;
    this->Display(oss) << endl;
    throw runtime_error(oss.str());
  }
  return v1 / v2;
}

ExpressionLog::ExpressionLog(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Log, e, false} {}

void ExpressionLog::check_domain(const double v) {
  if (!(v >= 0)) {
    ostringstream oss;
    oss << "log(" << v << ") : numerical argument out of domain. " << v
        << " is not in [0, +oo)" << endl;
    throw domain_error(oss.str());
  }
}

Expression ExpressionLog::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return log(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionLog::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return log(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionLog::Differentiate(const Variable& x) const {
  // ∂/∂x log(f) = (∂/∂x f) / f
  const Expression& f{get_argument()};
  return f.Differentiate(x) / f;
}

ostream& ExpressionLog::Display(ostream& os) const {
  return os << "log(" << get_argument() << ")";
}

double ExpressionLog::DoEvaluate(const double v) const {
  check_domain(v);
  return std::log(v);
}

ExpressionAbs::ExpressionAbs(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Abs, e, false} {}

Expression ExpressionAbs::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return abs(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAbs::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return abs(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAbs::Differentiate(const Variable& x) const {
  if (GetVariables().include(x)) {
    ostringstream oss;
    Display(oss) << "is not differentiable with respect to " << x << ".";
    throw runtime_error(oss.str());
  } else {
    return Expression::Zero();
  }
}

ostream& ExpressionAbs::Display(ostream& os) const {
  return os << "abs(" << get_argument() << ")";
}

double ExpressionAbs::DoEvaluate(const double v) const { return std::fabs(v); }

ExpressionExp::ExpressionExp(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Exp, e, false} {}

Expression ExpressionExp::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return exp(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionExp::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return exp(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionExp::Differentiate(const Variable& x) const {
  // ∂/∂x exp(f) = exp(f) * (∂/∂x f)
  const Expression& f{get_argument()};
  return exp(f) * f.Differentiate(x);
}

ostream& ExpressionExp::Display(ostream& os) const {
  return os << "exp(" << get_argument() << ")";
}

double ExpressionExp::DoEvaluate(const double v) const { return std::exp(v); }

ExpressionSqrt::ExpressionSqrt(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Sqrt, e, false} {}

void ExpressionSqrt::check_domain(const double v) {
  if (!(v >= 0)) {
    ostringstream oss;
    oss << "sqrt(" << v << ") : numerical argument out of domain. " << v
        << " is not in [0, +oo)" << endl;
    throw domain_error(oss.str());
  }
}

Expression ExpressionSqrt::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return sqrt(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSqrt::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return sqrt(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSqrt::Differentiate(const Variable& x) const {
  // ∂/∂x (sqrt(f)) = 1 / (2 * sqrt(f)) * (∂/∂x f)
  const Expression& f{get_argument()};
  return 1 / (2 * sqrt(f)) * f.Differentiate(x);
}

ostream& ExpressionSqrt::Display(ostream& os) const {
  return os << "sqrt(" << get_argument() << ")";
}

double ExpressionSqrt::DoEvaluate(const double v) const {
  check_domain(v);
  return std::sqrt(v);
}

ExpressionPow::ExpressionPow(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Pow, e1, e2,
                           determine_polynomial(e1, e2)} {}

void ExpressionPow::check_domain(const double v1, const double v2) {
  if (std::isfinite(v1) && (v1 < 0.0) && std::isfinite(v2) && !is_integer(v2)) {
    ostringstream oss;
    oss << "pow(" << v1 << ", " << v2
        << ") : numerical argument out of domain. " << v1
        << " is finite negative and " << v2 << " is finite non-integer."
        << endl;
    throw domain_error(oss.str());
  }
}

Expression ExpressionPow::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_expanded{arg1.Expand()};
  const Expression arg2_expanded{arg2.Expand()};
  if (!arg1.EqualTo(arg1_expanded) || !arg2.EqualTo(arg2_expanded)) {
    return ExpandPow(arg1_expanded, arg2_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionPow::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return pow(arg1_subst, arg2_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionPow::Differentiate(const Variable& x) const {
  return DifferentiatePow(get_first_argument(), get_second_argument(), x);
}

ostream& ExpressionPow::Display(ostream& os) const {
  return os << "pow(" << get_first_argument() << ", " << get_second_argument()
            << ")";
}

double ExpressionPow::DoEvaluate(const double v1, const double v2) const {
  check_domain(v1, v2);
  return std::pow(v1, v2);
}

ExpressionSin::ExpressionSin(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Sin, e, false} {}

Expression ExpressionSin::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return sin(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSin::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return sin(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSin::Differentiate(const Variable& x) const {
  // ∂/∂x (sin f) = (cos f) * (∂/∂x f)
  const Expression& f{get_argument()};
  return cos(f) * f.Differentiate(x);
}

ostream& ExpressionSin::Display(ostream& os) const {
  return os << "sin(" << get_argument() << ")";
}

double ExpressionSin::DoEvaluate(const double v) const { return std::sin(v); }

ExpressionCos::ExpressionCos(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Cos, e, false} {}

Expression ExpressionCos::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return cos(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionCos::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return cos(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionCos::Differentiate(const Variable& x) const {
  // ∂/∂x (cos f) = - (sin f) * (∂/∂x f)
  const Expression& f{get_argument()};
  return -sin(f) * f.Differentiate(x);
}

ostream& ExpressionCos::Display(ostream& os) const {
  return os << "cos(" << get_argument() << ")";
}

double ExpressionCos::DoEvaluate(const double v) const { return std::cos(v); }

ExpressionTan::ExpressionTan(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Tan, e, false} {}

Expression ExpressionTan::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return tan(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionTan::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return tan(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionTan::Differentiate(const Variable& x) const {
  // ∂/∂x (tan f) = (1 / (cos f)^2) * (∂/∂x f)
  const Expression& f{get_argument()};
  return (1 / pow(cos(f), 2)) * f.Differentiate(x);
}

ostream& ExpressionTan::Display(ostream& os) const {
  return os << "tan(" << get_argument() << ")";
}

double ExpressionTan::DoEvaluate(const double v) const { return std::tan(v); }

ExpressionAsin::ExpressionAsin(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Asin, e, false} {}

void ExpressionAsin::check_domain(const double v) {
  if (!((v >= -1.0) && (v <= 1.0))) {
    ostringstream oss;
    oss << "asin(" << v << ") : numerical argument out of domain. " << v
        << " is not in [-1.0, +1.0]" << endl;
    throw domain_error(oss.str());
  }
}

Expression ExpressionAsin::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return asin(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAsin::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return asin(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAsin::Differentiate(const Variable& x) const {
  // ∂/∂x (asin f) = (1 / sqrt(1 - f^2)) (∂/∂x f)
  const Expression& f{get_argument()};
  return (1 / sqrt(1 - pow(f, 2))) * f.Differentiate(x);
}

ostream& ExpressionAsin::Display(ostream& os) const {
  return os << "asin(" << get_argument() << ")";
}

double ExpressionAsin::DoEvaluate(const double v) const {
  check_domain(v);
  return std::asin(v);
}

ExpressionAcos::ExpressionAcos(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Acos, e, false} {}

void ExpressionAcos::check_domain(const double v) {
  if (!((v >= -1.0) && (v <= 1.0))) {
    ostringstream oss;
    oss << "acos(" << v << ") : numerical argument out of domain. " << v
        << " is not in [-1.0, +1.0]" << endl;
    throw domain_error(oss.str());
  }
}

Expression ExpressionAcos::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return acos(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAcos::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return acos(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAcos::Differentiate(const Variable& x) const {
  // ∂/∂x (acos f) = - 1 / sqrt(1 - f^2) * (∂/∂x f)
  const Expression& f{get_argument()};
  return -1 / sqrt(1 - pow(f, 2)) * f.Differentiate(x);
}

ostream& ExpressionAcos::Display(ostream& os) const {
  return os << "acos(" << get_argument() << ")";
}

double ExpressionAcos::DoEvaluate(const double v) const {
  check_domain(v);
  return std::acos(v);
}

ExpressionAtan::ExpressionAtan(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Atan, e, false} {}

Expression ExpressionAtan::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return atan(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAtan::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return atan(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAtan::Differentiate(const Variable& x) const {
  // ∂/∂x (atan f) = (1 / (1 + f^2)) * ∂/∂x f
  const Expression& f{get_argument()};
  return (1 / (1 + pow(f, 2))) * f.Differentiate(x);
}

ostream& ExpressionAtan::Display(ostream& os) const {
  return os << "atan(" << get_argument() << ")";
}

double ExpressionAtan::DoEvaluate(const double v) const { return std::atan(v); }

ExpressionAtan2::ExpressionAtan2(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Atan2, e1, e2, false} {}

Expression ExpressionAtan2::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_expanded{arg1.Expand()};
  const Expression arg2_expanded{arg2.Expand()};
  if (!arg1.EqualTo(arg1_expanded) || !arg2.EqualTo(arg2_expanded)) {
    return atan2(arg1_expanded, arg2_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAtan2::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return atan2(arg1_subst, arg2_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionAtan2::Differentiate(const Variable& x) const {
  // ∂/∂x (atan2(f,g)) = (g * (∂/∂x f) - f * (∂/∂x g)) / (f^2 + g^2)
  const Expression& f{get_first_argument()};
  const Expression& g{get_second_argument()};
  return (g * f.Differentiate(x) - f * g.Differentiate(x)) /
         (pow(f, 2) + pow(g, 2));
}

ostream& ExpressionAtan2::Display(ostream& os) const {
  return os << "atan2(" << get_first_argument() << ", " << get_second_argument()
            << ")";
}

double ExpressionAtan2::DoEvaluate(const double v1, const double v2) const {
  return std::atan2(v1, v2);
}

ExpressionSinh::ExpressionSinh(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Sinh, e, false} {}

Expression ExpressionSinh::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return sinh(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSinh::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return sinh(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionSinh::Differentiate(const Variable& x) const {
  // ∂/∂x (sinh f) = cosh(f) * (∂/∂x f)
  const Expression& f{get_argument()};
  return cosh(f) * f.Differentiate(x);
}

ostream& ExpressionSinh::Display(ostream& os) const {
  return os << "sinh(" << get_argument() << ")";
}

double ExpressionSinh::DoEvaluate(const double v) const { return std::sinh(v); }

ExpressionCosh::ExpressionCosh(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Cosh, e, false} {}

Expression ExpressionCosh::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return cosh(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionCosh::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return cosh(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionCosh::Differentiate(const Variable& x) const {
  // ∂/∂x (cosh f) = sinh(f) * (∂/∂x f)
  const Expression& f{get_argument()};
  return sinh(f) * f.Differentiate(x);
}

ostream& ExpressionCosh::Display(ostream& os) const {
  return os << "cosh(" << get_argument() << ")";
}

double ExpressionCosh::DoEvaluate(const double v) const { return std::cosh(v); }

ExpressionTanh::ExpressionTanh(const Expression& e)
    : UnaryExpressionCell{ExpressionKind::Tanh, e, false} {}

Expression ExpressionTanh::Expand() const {
  const Expression& arg{get_argument()};
  const Expression arg_expanded{arg.Expand()};
  if (!arg.EqualTo(arg_expanded)) {
    return tanh(arg_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionTanh::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg{get_argument()};
  const Expression arg_subst{arg.Substitute(expr_subst, formula_subst)};
  if (!arg.EqualTo(arg_subst)) {
    return tanh(arg_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionTanh::Differentiate(const Variable& x) const {
  // ∂/∂x (tanh f) = 1 / (cosh^2(f)) * (∂/∂x f)
  const Expression& f{get_argument()};
  return 1 / pow(cosh(f), 2) * f.Differentiate(x);
}

ostream& ExpressionTanh::Display(ostream& os) const {
  return os << "tanh(" << get_argument() << ")";
}

double ExpressionTanh::DoEvaluate(const double v) const { return std::tanh(v); }

ExpressionMin::ExpressionMin(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Min, e1, e2, false} {}

Expression ExpressionMin::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_expanded{arg1.Expand()};
  const Expression arg2_expanded{arg2.Expand()};
  if (!arg1.EqualTo(arg1_expanded) || !arg2.EqualTo(arg2_expanded)) {
    return min(arg1_expanded, arg2_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionMin::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return min(arg1_subst, arg2_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionMin::Differentiate(const Variable& x) const {
  if (GetVariables().include(x)) {
    ostringstream oss;
    Display(oss) << "is not differentiable with respect to " << x << ".";
    throw runtime_error(oss.str());
  } else {
    return Expression::Zero();
  }
}

ostream& ExpressionMin::Display(ostream& os) const {
  return os << "min(" << get_first_argument() << ", " << get_second_argument()
            << ")";
}

double ExpressionMin::DoEvaluate(const double v1, const double v2) const {
  return std::min(v1, v2);
}

ExpressionMax::ExpressionMax(const Expression& e1, const Expression& e2)
    : BinaryExpressionCell{ExpressionKind::Max, e1, e2, false} {}

Expression ExpressionMax::Expand() const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_expanded{arg1.Expand()};
  const Expression arg2_expanded{arg2.Expand()};
  if (!arg1.EqualTo(arg1_expanded) || !arg2.EqualTo(arg2_expanded)) {
    return max(arg1_expanded, arg2_expanded);
  } else {
    return GetExpression();
  }
}

Expression ExpressionMax::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Expression& arg1{get_first_argument()};
  const Expression& arg2{get_second_argument()};
  const Expression arg1_subst{arg1.Substitute(expr_subst, formula_subst)};
  const Expression arg2_subst{arg2.Substitute(expr_subst, formula_subst)};
  if (!arg1.EqualTo(arg1_subst) || !arg2.EqualTo(arg2_subst)) {
    return max(arg1_subst, arg2_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionMax::Differentiate(const Variable& x) const {
  if (GetVariables().include(x)) {
    ostringstream oss;
    Display(oss) << "is not differentiable with respect to " << x << ".";
    throw runtime_error(oss.str());
  } else {
    return Expression::Zero();
  }
}

ostream& ExpressionMax::Display(ostream& os) const {
  return os << "max(" << get_first_argument() << ", " << get_second_argument()
            << ")";
}

double ExpressionMax::DoEvaluate(const double v1, const double v2) const {
  return std::max(v1, v2);
}

// ExpressionIfThenElse
// --------------------
ExpressionIfThenElse::ExpressionIfThenElse(const Formula& f_cond,
                                           const Expression& e_then,
                                           const Expression& e_else)
    : ExpressionCell{ExpressionKind::IfThenElse,
                     hash_combine(hash_value<Formula>{}(f_cond), e_then,
                                  e_else),
                     false},
      f_cond_{f_cond},
      e_then_{e_then},
      e_else_{e_else} {}

Variables ExpressionIfThenElse::GetVariables() const {
  Variables ret{f_cond_.GetFreeVariables()};
  ret.insert(e_then_.GetVariables());
  ret.insert(e_else_.GetVariables());
  return ret;
}

bool ExpressionIfThenElse::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const ExpressionIfThenElse& ite_e{
      static_cast<const ExpressionIfThenElse&>(e)};
  return f_cond_.EqualTo(ite_e.f_cond_) && e_then_.EqualTo(ite_e.e_then_) &&
         e_else_.EqualTo(ite_e.e_else_);
}

bool ExpressionIfThenElse::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const ExpressionIfThenElse& ite_e{
      static_cast<const ExpressionIfThenElse&>(e)};
  if (f_cond_.Less(ite_e.f_cond_)) {
    return true;
  }
  if (ite_e.f_cond_.Less(f_cond_)) {
    return false;
  }
  if (e_then_.Less(ite_e.e_then_)) {
    return true;
  }
  if (ite_e.e_then_.Less(e_then_)) {
    return false;
  }
  return e_else_.Less(ite_e.e_else_);
}

double ExpressionIfThenElse::Evaluate(const Environment& env) const {
  if (f_cond_.Evaluate(env)) {
    return e_then_.Evaluate(env);
  }
  return e_else_.Evaluate(env);
}

Expression ExpressionIfThenElse::Expand() const {
  // TODO(soonho): use the following line when Formula::Expand() is implemented.
  // return if_then_else(f_cond_.Expand(), e_then_.Expand(), e_else_.Expand());
  throw runtime_error("Not yet implemented.");
}

Expression ExpressionIfThenElse::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  const Formula f_cond_subst{f_cond_.Substitute(expr_subst, formula_subst)};
  const Expression e_then_subst{e_then_.Substitute(expr_subst, formula_subst)};
  const Expression e_else_subst{e_else_.Substitute(expr_subst, formula_subst)};
  if (!f_cond_.EqualTo(f_cond_subst) || !e_then_.EqualTo(e_then_subst) ||
      !e_else_.EqualTo(e_else_subst)) {
    return if_then_else(f_cond_subst, e_then_subst, e_else_subst);
  } else {
    return GetExpression();
  }
}

Expression ExpressionIfThenElse::Differentiate(const Variable& x) const {
  if (GetVariables().include(x)) {
    ostringstream oss;
    Display(oss) << "is not differentiable with respect to " << x << ".";
    throw runtime_error(oss.str());
  } else {
    return Expression::Zero();
  }
}

ostream& ExpressionIfThenElse::Display(ostream& os) const {
  return os << "(if " << f_cond_ << " then " << e_then_ << " else " << e_else_
            << ")";
}

// ExpressionUninterpretedFunction
// --------------------
ExpressionUninterpretedFunction::ExpressionUninterpretedFunction(
    const string& name, const Variables& vars)
    : ExpressionCell{ExpressionKind::UninterpretedFunction,
                     hash_combine(hash_value<string>{}(name), vars), false},
      name_{name},
      variables_{vars} {}

Variables ExpressionUninterpretedFunction::GetVariables() const {
  return variables_;
}

bool ExpressionUninterpretedFunction::EqualTo(const ExpressionCell& e) const {
  // Expression::EqualTo guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const ExpressionUninterpretedFunction& uf_e{
      static_cast<const ExpressionUninterpretedFunction&>(e)};
  return name_ == uf_e.name_ && variables_ == uf_e.variables_;
}

bool ExpressionUninterpretedFunction::Less(const ExpressionCell& e) const {
  // Expression::Less guarantees the following assertion.
  assert(get_kind() == e.get_kind());
  const ExpressionUninterpretedFunction& uf_e{
      static_cast<const ExpressionUninterpretedFunction&>(e)};
  if (name_ < uf_e.name_) {
    return true;
  }
  if (uf_e.name_ < name_) {
    return false;
  }
  return variables_ < uf_e.variables_;
}

double ExpressionUninterpretedFunction::Evaluate(const Environment&) const {
  throw runtime_error("Uninterpreted-function expression cannot be evaluated.");
}

Expression ExpressionUninterpretedFunction::Expand() const {
  return GetExpression();
}

Expression ExpressionUninterpretedFunction::Substitute(
    const ExpressionSubstitution& expr_subst,
    const FormulaSubstitution& formula_subst) const {
  // This method implements the following substitution:
  //     uf(name, {v₁, ..., vₙ}).Substitute(expr_subst, formula_subst)
  //   = uf(name, ⋃ᵢ (expr_subst[vᵢ].GetVariables() ∪ formula_subst[vᵢ])
  //
  // For example, we have:
  //     uf("uf1", {x, y, b}).Substitute({x ↦ 1, y ↦ y + z}, {b ↦ x > 0})
  //   = uf("uf1", ∅ ∪ {y, z} ∪ {x})
  //   = uf("uf1", {x, y, z}).
  Variables new_vars;
  for (const auto& var : variables_) {
    if (var.get_type() == Variable::Type::BOOLEAN) {
      if (formula_subst.count(var) > 0) {
        new_vars += formula_subst.at(var).GetFreeVariables();
      }
    } else {
      if (expr_subst.count(var) > 0) {
        new_vars += expr_subst.at(var).GetVariables();
      }
    }
  }
  return uninterpreted_function(name_, new_vars);
}

Expression ExpressionUninterpretedFunction::Differentiate(
    const Variable& x) const {
  if (variables_.include(x)) {
    // This uninterpreted function does have `x` as an argument, but we don't
    // have sufficient information to differentiate it with respect to `x`.
    ostringstream oss;
    oss << "Uninterpreted-function expression ";
    Display(oss);
    oss << " is not differentiable with respect to " << x << ".";
    throw runtime_error(oss.str());
  } else {
    // `x` is free in this uninterpreted function.
    return Expression::Zero();
  }
}

ostream& ExpressionUninterpretedFunction::Display(ostream& os) const {
  return os << name_ << "(" << variables_ << ")";
}

bool is_constant(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Constant;
}
bool is_real_constant(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::RealConstant;
}
bool is_variable(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Var;
}
bool is_addition(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Add;
}
bool is_multiplication(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Mul;
}
bool is_division(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Div;
}
bool is_log(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Log;
}
bool is_abs(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Abs;
}
bool is_exp(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Exp;
}
bool is_sqrt(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Sqrt;
}
bool is_pow(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Pow;
}
bool is_sin(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Sin;
}
bool is_cos(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Cos;
}
bool is_tan(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Tan;
}
bool is_asin(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Asin;
}
bool is_acos(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Acos;
}
bool is_atan(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Atan;
}
bool is_atan2(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Atan2;
}
bool is_sinh(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Sinh;
}
bool is_cosh(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Cosh;
}
bool is_tanh(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Tanh;
}
bool is_min(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Min;
}
bool is_max(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::Max;
}
bool is_if_then_else(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::IfThenElse;
}
bool is_uninterpreted_function(const ExpressionCell& c) {
  return c.get_kind() == ExpressionKind::UninterpretedFunction;
}

const ExpressionConstant* to_constant(const ExpressionCell* const expr_ptr) {
  assert(is_constant(*expr_ptr));
  return static_cast<const ExpressionConstant*>(expr_ptr);
}
const ExpressionConstant* to_constant(const Expression& e) {
  return to_constant(e.ptr_);
}

const ExpressionRealConstant* to_real_constant(
    const ExpressionCell* const expr_ptr) {
  assert(is_real_constant(*expr_ptr));
  return static_cast<const ExpressionRealConstant*>(expr_ptr);
}
const ExpressionRealConstant* to_real_constant(const Expression& e) {
  return to_real_constant(e.ptr_);
}

const ExpressionVar* to_variable(const ExpressionCell* const expr_ptr) {
  assert(is_variable(*expr_ptr));
  return static_cast<const ExpressionVar*>(expr_ptr);
}
const ExpressionVar* to_variable(const Expression& e) {
  return to_variable(e.ptr_);
}

const UnaryExpressionCell* to_unary(const ExpressionCell* const expr_ptr) {
  assert(is_log(*expr_ptr) || is_abs(*expr_ptr) || is_exp(*expr_ptr) ||
         is_sqrt(*expr_ptr) || is_sin(*expr_ptr) || is_cos(*expr_ptr) ||
         is_tan(*expr_ptr) || is_asin(*expr_ptr) || is_acos(*expr_ptr) ||
         is_atan(*expr_ptr) || is_sinh(*expr_ptr) || is_cosh(*expr_ptr) ||
         is_tanh(*expr_ptr));
  return static_cast<const UnaryExpressionCell*>(expr_ptr);
}
const UnaryExpressionCell* to_unary(const Expression& e) {
  return to_unary(e.ptr_);
}

const BinaryExpressionCell* to_binary(const ExpressionCell* const expr_ptr) {
  assert(is_addition(*expr_ptr) || is_multiplication(*expr_ptr) ||
         is_division(*expr_ptr) || is_pow(*expr_ptr) || is_atan2(*expr_ptr) ||
         is_min(*expr_ptr) || is_max(*expr_ptr));
  return static_cast<const BinaryExpressionCell*>(expr_ptr);
}
const BinaryExpressionCell* to_binary(const Expression& e) {
  return to_binary(e.ptr_);
}

const ExpressionAdd* to_addition(const ExpressionCell* const expr_ptr) {
  assert(is_addition(*expr_ptr));
  return static_cast<const ExpressionAdd*>(expr_ptr);
}
const ExpressionAdd* to_addition(const Expression& e) {
  return to_addition(e.ptr_);
}

const ExpressionMul* to_multiplication(const ExpressionCell* const expr_ptr) {
  assert(is_multiplication(*expr_ptr));
  return static_cast<const ExpressionMul*>(expr_ptr);
}
const ExpressionMul* to_multiplication(const Expression& e) {
  return to_multiplication(e.ptr_);
}

const ExpressionDiv* to_division(const ExpressionCell* const expr_ptr) {
  assert(is_division(*expr_ptr));
  return static_cast<const ExpressionDiv*>(expr_ptr);
}
const ExpressionDiv* to_division(const Expression& e) {
  return to_division(e.ptr_);
}

const ExpressionLog* to_log(const ExpressionCell* const expr_ptr) {
  assert(is_log(*expr_ptr));
  return static_cast<const ExpressionLog*>(expr_ptr);
}
const ExpressionLog* to_log(const Expression& e) { return to_log(e.ptr_); }

const ExpressionAbs* to_abs(const ExpressionCell* const expr_ptr) {
  assert(is_abs(*expr_ptr));
  return static_cast<const ExpressionAbs*>(expr_ptr);
}
const ExpressionAbs* to_abs(const Expression& e) { return to_abs(e.ptr_); }

const ExpressionExp* to_exp(const ExpressionCell* const expr_ptr) {
  assert(is_exp(*expr_ptr));
  return static_cast<const ExpressionExp*>(expr_ptr);
}
const ExpressionExp* to_exp(const Expression& e) { return to_exp(e.ptr_); }

const ExpressionSqrt* to_sqrt(const ExpressionCell* const expr_ptr) {
  assert(is_sqrt(*expr_ptr));
  return static_cast<const ExpressionSqrt*>(expr_ptr);
}
const ExpressionSqrt* to_sqrt(const Expression& e) { return to_sqrt(e.ptr_); }
const ExpressionPow* to_pow(const ExpressionCell* const expr_ptr) {
  assert(is_pow(*expr_ptr));
  return static_cast<const ExpressionPow*>(expr_ptr);
}
const ExpressionPow* to_pow(const Expression& e) { return to_pow(e.ptr_); }

const ExpressionSin* to_sin(const ExpressionCell* const expr_ptr) {
  assert(is_sin(*expr_ptr));
  return static_cast<const ExpressionSin*>(expr_ptr);
}
const ExpressionSin* to_sin(const Expression& e) { return to_sin(e.ptr_); }

const ExpressionCos* to_cos(const ExpressionCell* const expr_ptr) {
  assert(is_cos(*expr_ptr));
  return static_cast<const ExpressionCos*>(expr_ptr);
}
const ExpressionCos* to_cos(const Expression& e) { return to_cos(e.ptr_); }

const ExpressionTan* to_tan(const ExpressionCell* const expr_ptr) {
  assert(is_tan(*expr_ptr));
  return static_cast<const ExpressionTan*>(expr_ptr);
}
const ExpressionTan* to_tan(const Expression& e) { return to_tan(e.ptr_); }

const ExpressionAsin* to_asin(const ExpressionCell* const expr_ptr) {
  assert(is_asin(*expr_ptr));
  return static_cast<const ExpressionAsin*>(expr_ptr);
}
const ExpressionAsin* to_asin(const Expression& e) { return to_asin(e.ptr_); }

const ExpressionAcos* to_acos(const ExpressionCell* const expr_ptr) {
  assert(is_acos(*expr_ptr));
  return static_cast<const ExpressionAcos*>(expr_ptr);
}
const ExpressionAcos* to_acos(const Expression& e) { return to_acos(e.ptr_); }

const ExpressionAtan* to_atan(const ExpressionCell* const expr_ptr) {
  assert(is_atan(*expr_ptr));
  return static_cast<const ExpressionAtan*>(expr_ptr);
}
const ExpressionAtan* to_atan(const Expression& e) { return to_atan(e.ptr_); }

const ExpressionAtan2* to_atan2(const ExpressionCell* const expr_ptr) {
  assert(is_atan2(*expr_ptr));
  return static_cast<const ExpressionAtan2*>(expr_ptr);
}
const ExpressionAtan2* to_atan2(const Expression& e) {
  return to_atan2(e.ptr_);
}

const ExpressionSinh* to_sinh(const ExpressionCell* const expr_ptr) {
  assert(is_sinh(*expr_ptr));
  return static_cast<const ExpressionSinh*>(expr_ptr);
}
const ExpressionSinh* to_sinh(const Expression& e) { return to_sinh(e.ptr_); }

const ExpressionCosh* to_cosh(const ExpressionCell* const expr_ptr) {
  assert(is_cosh(*expr_ptr));
  return static_cast<const ExpressionCosh*>(expr_ptr);
}
const ExpressionCosh* to_cosh(const Expression& e) { return to_cosh(e.ptr_); }

const ExpressionTanh* to_tanh(const ExpressionCell* const expr_ptr) {
  assert(is_tanh(*expr_ptr));
  return static_cast<const ExpressionTanh*>(expr_ptr);
}
const ExpressionTanh* to_tanh(const Expression& e) { return to_tanh(e.ptr_); }

const ExpressionMin* to_min(const ExpressionCell* const expr_ptr) {
  assert(is_min(*expr_ptr));
  return static_cast<const ExpressionMin*>(expr_ptr);
}
const ExpressionMin* to_min(const Expression& e) { return to_min(e.ptr_); }

const ExpressionMax* to_max(const ExpressionCell* const expr_ptr) {
  assert(is_max(*expr_ptr));
  return static_cast<const ExpressionMax*>(expr_ptr);
}
const ExpressionMax* to_max(const Expression& e) { return to_max(e.ptr_); }

const ExpressionIfThenElse* to_if_then_else(
    const ExpressionCell* const expr_ptr) {
  assert(is_if_then_else(*expr_ptr));
  return static_cast<const ExpressionIfThenElse*>(expr_ptr);
}
const ExpressionIfThenElse* to_if_then_else(const Expression& e) {
  return to_if_then_else(e.ptr_);
}

const ExpressionUninterpretedFunction* to_uninterpreted_function(
    const ExpressionCell* const expr_ptr) {
  assert(is_uninterpreted_function(*expr_ptr));
  return static_cast<const ExpressionUninterpretedFunction*>(expr_ptr);
}
const ExpressionUninterpretedFunction* to_uninterpreted_function(
    const Expression& e) {
  return to_uninterpreted_function(e.ptr_);
}

}  // namespace symbolic
}  // namespace drake
}  // namespace dreal
