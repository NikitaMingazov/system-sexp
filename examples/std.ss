; set a symbol in the current scope's value
(p-st-set (p-root-scope) 'let
    '(()
      (p-macrobody (argc argv)
                   (p-st-set (p-parent-scope (p-scope) 4)
                             (p-deref argv)
                             (p-multi-eval
                               (p-deref (p-uadd argv (p-sexp-size)))
                               ())))))

(let sexp-size (p-sexp-size))
(let typeof p-typeof)
; typeof results
(let :symbol 1)
(let :string 2)
(let :sval 4)
(let :list 7)
(let \ p-deref)
(let u<  p-ult)
(let u<= p-ulte)
(let u>  p-ugt)
(let u>= p-ugte)
(let s<  p-slt)
(let s<= p-slte)
(let s>  p-sgt)
(let s>= p-sgte)
(let u+  p-uadd)
(let u-  p-usub)
(let u*  p-umul)
(let s+  p-sadd)
(let s-  p-ssub)
(let s*  p-smul)

; eval-twice
(let eval-2
  '(()
    (p-macrobody (_ argv)
                 (p-multi-eval
                   (\ argv)
                   ()
                   ()))))

; array access
(let []
  '(()
    (p-macrobody (_ argv)
                 (\ (u+ (eval-2 (\ argv))
                        (u* sexp-size
                            (eval-2 (\ (u+ argv
                                           sexp-size)))))))))

(let eval-arg
  '(()
    (p-macrobody (argc argv)
                 (eval-2 ([] ([] argv 0)
                             ([] argv 1))))))

(let progn
  '(()
    (p-macrobody (argc argv)
                 (let first (eval-arg argv 0))
                 (p-if (u<= argc 1)
                       first  ; end of args, return last
                       (p-slice-eval progn  ; more args, recurse
                                     (u- argc 1)
                                     (u+ argv sexp-size))))))

; evals the symbol arg
(let letv
    '(()
      (p-macrobody (argc argv)
                   (p-print "line 0\n")
                   (let argv-copy (p-alloca (u* argc sexp-size)))
                   (p-print "line 1\n")
                   (p-memcpy argv-copy
                             (p-adrof (eval-arg 0))
                             sexp-size)
                   (p-print "line 2\n")
                   (p-memcpy (u+ argv-copy sexp-size)
                             (u+ argv sexp-size)
                             sexp-size)
                   (p-print "line 3\n")
                   (p-slice-eval let argc argv-copy))))
; (letv 'x 5)
