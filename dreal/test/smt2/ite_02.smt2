(set-logic QF_LRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (= x 10))
(assert (= y -10))
(assert (= 2 (ite (> x 0) (ite (> y 0) 1 2) (ite (> y 0) 3 4))))
(check-sat)
(exit)
