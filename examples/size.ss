; TODO: let calls symboltable-set 1 above, make it general
; (p-st-set 1 let
;   (()
;    (p-st-set 1 (p-deref argv)))

(p-let let p-let)
(let sexp-size (p-sexp_size))
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
                 (p-multi_eval
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

; debugging (TODO)
; top-level
; (let eval
;   '(()
;     (p-macrobody (_ argv)
;                  (p-print "Called eval on:\n  ")
;                  (p-print (p-format ([] argv 0)))
;                  (p-print "\n in repl: type y to go into children, n to not\n")
;                  (let input (p-read))
;                  (p-while
;                  (eval-2
;                  (let pre-eval p-quote)
;                  (eval-arg argv 0))))))


(let progn
  '(()
    (p-macrobody (argc argv)
                 (let first (eval-arg argv 0))
                 (eval-2
                   (p-if (p-ulte argc 1)
                     first  ; end of args, return last
                     (p-slice_eval progn  ; more args, recurse
                                    (u- argc 1)
                                    (u+ argv sexp-size)))))))

(let print
  '(()
    (p-macrobody (argc argv)
                 (eval-2
                   (p-if (u> argc 0)
                     (progn (p-print (p-format (eval-arg argv 0)))
                            (p-slice_eval print
                                          (u- argc 1)
                                          (u+ argv sexp-size)))
                     ())))))

(let println
  '(()
    (p-macrobody (argc argv)
                 (p-slice_eval print argc argv)
                 (print "\n"))))

(println
  "A sexp is " sexp-size " bytes")

; sexp asignment to symbol
(let =
  '(()
    (p-macrobody (argc argv)
                 (let val
                   (eval-arg argv 1))
                 (p-memcpy
                   (p-get (eval-2 ([] argv 0)))
                   (p-adrof (eval-2 val))
                   sexp-size))))

(let +=
  '(()
    (p-macrobody (argc argv)
                 (= ([] argv 0)
                    (s+ (eval-arg argv 0)
                        (eval-arg argv 1))))))

(let i 5)
(p-while (s> i 0)
   (println i)
   (+= i -1))
