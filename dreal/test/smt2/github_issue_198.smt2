;; From https://github.com/dreal/dreal4/issues/198
(set-logic QF_NRA)
(set-option :worklist-fixpoint true)
(declare-fun _substvar_12_ () Real)
(declare-const r5 Real)
(check-sat)
(assert (= _substvar_12_ r5))
(check-sat)
(declare-const r10 Real)
(declare-const v15 Bool)
(assert (and (<= 0.7 r10) v15))
(check-sat)
