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

(let c-print
  '(()
    (p-macrobody (argc argv)
                 (eval-2
                   (p-if (u> argc 0)
                     (progn (p-print (p-format (eval-arg argv 0)))
                            (p-slice_eval c-print
                                          (u- argc 1)
                                          (u+ argv sexp-size)))
                     ())))))

(let print
  '(()
    (p-macrobody (argc argv)
                 (let val (eval-arg argv 0))
                 (p-print (p-format val))
                 (print "\n")
                 val)))

(let define
  '(()
    (p-macrobody (argc argv)
                 (let _fname ([] argv 0))
                 (let fargs ([] argv 1))
                 (let num-args (p-length fargs))
                 (let macro-len (u+ (num-args)
                                    (p-length ([] argv 2))))
                 ; todo: come up with something better than malloc
                 (let arr (p-malloc (u* macro-len sexp-size)))
                 (let fn (p_list arr macro-len))
                 (let i 0)
                 ; prepend arg bindings to the body
                 (while (s< i num_args)
                        (p-memcpy
                          (p-nth fn i)
                          (p-adrof (p-replace
                                     (p-replace
                                       '(let `ARG ([] argv `I))
                                       '`I
                                       i)
                                     '`ARG
                                     (\ (p-nth fargs i))))
                          sexp-size)
                        (+= i 1))
                 (p-memcpy
                   (p-nth fn i)
                   (p-nth ([] argv 2) 0)
                   sexp-size)
                 (p-set _fname fn)
                 ([] argv 0))))
(let if
  '(()
    (p-macrobody (argc argv)
                 (eval-2 (p-slice_eval p-if argc argv)))))
(let while p-while)
(let begin progn)
(let set p-set)
(let + p-sadd)
(let - p-ssub)
(let * p-smul)
(let / p-sdiv)
(let = p-biteq)
(let < p-slt)
(let > p-sgt)

(println
  (set x 4)
  (set y 5)
  (begin
    (print x)
    (print y)
    (* x y))
  (while (> y 0)
         (begin (set x (+ x x))
                (set y (- y 1))))
  x)
