(set-logic QF_NRA)

(declare-fun x () Real)

(assert
  (< x
    (/ -5958794406280026737321749580044627371847784122820048012901591655062228043291584529320676278333634395479263695438352461080903866550657496326983330733939397122608118001752501366467078247679639248858985308475645248852528421752796443859977179980652986183631006035696164936589846712144996689667535556521700877530526056136831681938495273018295601429023268910164621445837592456216568037516054009760152112737778625102759932844096230189554513187632834566219070781559871632141159360310105873887881030113588478889194394136769
      2232727415076580820455037352120792611443113190203232546351347940953849902041751947669168711144617720671893465217755961028483893834536496684559005005242543837497203388022222335086445966289054121316350779770703674169606698911437418313809920000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
    )
  )
)

(check-sat)
(exit)