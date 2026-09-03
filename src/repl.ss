
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
(let \ p-deref)
(let u+  p-uadd)
(let u*  p-umul)

; eval-twice
(let eval-2
  '(()
    (p-macrobody (argc argv)
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

; sexp asignment to symbol
(let c:=
  '(()
    (p-macrobody (argc argv)
                 (let val
                   (eval-arg argv 1))
                 (p-memcpy
                   (p-get ([] argv 0))
                   (p-adrof val)
                   sexp-size))))

(let running 1)

(p-while running
  (p-set-in (p-stdin))
  (p-print "$ ")
  (let in-sexp (eval-2 (p-read)))
  (p-if (p-is-eof in-sexp)
    (c:= running 0)
    (p-print (p-format (eval-2 'in-sexp))))
  (p-print "\n"))
