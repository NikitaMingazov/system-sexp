; solutions to chapter 1 of Programming Languages: an Interpreted Approach
; language implementation, then programs in the language

; set a symbol in the current scope's value
(p-st-set (p-root-scope) 'let
    '(()
      (p-macrobody (argc argv)
                   (p-st-set (p-parent-scope (p-scope) 4)
                             (p-deref argv)
                             (p-multi-eval
                               (p-deref (p-uadd argv (p-sexp-size)))
                               ())))))

; eventually 'let' will be immutable
(let let-mut let)

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
    (p-macrobody (argc argv)
                 (p-multi-eval
                   (\ argv)
                   ()
                   ()))))

(let eval-at
  '(()
    (p-macrobody (argc argv)
                 (p-eval-at (eval-arg argv 0)
                            (eval-arg argv 1)
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

; allocates into the calltree two levels above
(let alloca
  '(()
    (p-macrobody (argc argv)
                 (p-ct-alloc (p-parent-scope (p-scope) 8)
                             (eval-arg argv 0)))))

; evals the symbol arg (TODO: make this actually work))
; (let letv
;     '(()
;       (p-macrobody (argc argv)
;                    (p-print "line 0\n")
;                    (let argv-copy (alloca argc sexp-size))
;                    (p-print "line 1\n")
;                    (p-memcpy argv-copy
;                              (p-adrof (eval-arg argv 0))
;                              sexp-size)
;                    (p-print "line 2\n")
;                    (p-memcpy (u+ argv-copy sexp-size)
;                              (u+ argv sexp-size)
;                              sexp-size)
;                    (p-print "line 3\n")
;                    (eval-at (p-slice-eval let argc argv-copy)
;                             (p-parent-scope (p-scope) 5)
;                             ()))))

(p-st-set (p-root-scope) 'letv
    '(()
      (p-macrobody (argc argv)
                   (p-st-set (p-parent-scope (p-scope) 4)
                             (eval-arg argv 0)
                             (eval-2 (p-deref (p-uadd argv (p-sexp-size))))))))


; (p-load "std.ss")

; sexp asignment to symbol
(let c:=
  '(()
    (p-macrobody (argc argv)
                 (let val
                   (eval-arg argv 1))
                 (p-memcpy
                   (p-get (eval-2 ([] argv 0)))
                   (p-adrof (eval-2 val))
                   sexp-size))))

(let c:+=
  '(()
    (p-macrobody (argc argv)
                 (c:= ([] argv 0)
                    (s+ (eval-arg argv 0)
                        (eval-arg argv 1))))))

(let print
  '(()
    (p-macrobody (argc argv)
                 (let val (eval-arg argv 0))
                 (p-print (p-format val))
                 (p-print "\n")
                 val)))

(let define
  '(()
    (p-macrobody (argc argv)
                 (let env (p-root-scope))
                 (let _fname ([] argv 0))
                 (let fargs ([] argv 1))
                 (let num-args (p-length fargs))
                 ; +2 for p-macrobody and argv binding
                 ; -2 for name and args
                 (let macro-len (u+ (u+ 2 num-args)
                                    (u- argc 2)))
                 ; TODO: come up with something better than malloc
                 (let arr (p-malloc (u* macro-len sexp-size)))
                 ; init to ()s
                 (let i 0)
                 (p-while (s< i macro-len)
                    (p-memcpy (u+ arr (u* i sexp-size))
                              (p-adrof ())
                              sexp-size)
                    (c:+= i 1))
                 (let fn (p-list arr macro-len))
                 (p-memcpy (p-nth fn 0)
                           (p-adrof 'p-macrobody)
                           sexp-size)
                 (p-memcpy (p-nth fn 1)
                           (p-adrof '(fn-argc fn-argv))
                           sexp-size)
                 (let i 0)
                 ; prepend arg bindings to the body
                 (p-while (s< i num-args)
                        (p-memcpy
                          (p-nth fn (u+ i 2))
                          (p-adrof (p-replace
                                     (p-replace
                                       '(let `ARG (eval-arg fn-argv `I))
                                       '`I
                                       i)
                                     '`ARG
                                     (\ (p-nth fargs i))))
                          sexp-size)
                        (c:+= i 1))
                 (p-memcpy
                   (p-nth fn (u+ i 2))
                   (p-adrof ([] argv 2))
                   sexp-size)
                 (p-set _fname (p-replace
                                 '(() fn)
                                 'fn
                                 fn))
                 ([] argv 0))))
(let if p-if)
(let while p-while)
(let begin progn)
(let set
  '(()
    (p-macrobody (argc argv)
                 (let name ([] argv 0))
                 (let val (eval-2 ([] argv 1)))
                 ; write to an existing symbol, make a new one if none
                 ; to properly handle globals and locals
                 (let scope (p-scope))
                 (if (p-st-get scope name)
                   (p-memcpy (p-st-get scope name)
                             (p-adrof val)
                             sexp-size)
                   (p-st-set (p-root-scope) name
                             val))
                 val)))
(let + p-sadd)
(let - p-ssub)
(let * p-smul)
(let / p-sdiv)
(let = p-biteq)
(let < p-slt)
(let > p-sgt)

; 1.
(define >= (k j)
  (> (+ k 1) j))
(define sigma (m n)
  (if (>= n m)
    (+ m (sigma (+ m 1) n))
    0))

; 2.
(define exp (m n)
  (if (> n 0)
    (* m (exp m (- n 1)))
    1))

; 3.
(define choose (n k)
  (if (= k 0)
    1
    (if (= k n)
      1
      (+ (choose (- n 1) k)
         (choose (- n 1) (- k 1))))))

; 4.
(define fib (m)
  (if (= m 0)
    0
    (if (= m 1)
      1
      (+ (fib (- m 1))
         (fib (- m 2))))))

; 5.
(define mod (m n) (- m (* n (/ m n))))
(define prime-helper (n i)
  (if (= n i)
    1
    (if (= 0 (mod n i))
      0
      (prime-helper n (+ i 1)))))
(define prime (n)
  (if (= n 1)
    0
    (if (= n 2)
      1
      (prime-helper n 2))))
(define nthhelper (n i counter)
  (if (prime i)
    (if (= n (+ counter 1))
      i
      (nthhelper n (+ i 1) (+ counter 1)))
    (nthhelper n (+ i 1) counter)))
(define nthprime (n)
  (nthhelper n 2 0))
(define sumhelper (n i counter)
  (if (prime i)
    (if (= n (+ counter 1))
      i
      (+ i (sumhelper n (+ i 1) (+ counter 1))))
    (sumhelper n (+ i 1) counter)))
(define sumprimes (n)
  (sumhelper n 2 0))
(define rel-helper (m n i)
  (if (> i m)
    1
    (if (> i n)
      1
      (if (= 0 (mod m i))
        (if (= 0 (mod n i))
          0
          (rel-helper m n (+ i 1)))
        (rel-helper m n (+ i 1))))))
(define relprime (m n)
  (if (= m 1)
    0
    (if (= n 1)
      0
      (rel-helper m n 2))))

; 6.
(define mod (m n) (- m (* n (/ m n))))
(define binary (m)
  (if (= m 0)
    0
    (if (= 1 (mod m 2))
      (+ 1 (* 10 (binary (/ m 2))))
      (* 10 (binary (/ m 2))))))

; 7.
(p-print "Question 7:\n")
(p-print "Expected output per Pascal:\n5\n4\n5\n=========\n")
(set x 2)
(define R (y) (begin (set x y) (print x))) ; x is global
(define Q (x) (begin (R (+ x 1)) (print x))) ; x is local
(Q 4)
(print x);

; 8.
(let read
  '(()
    (p-macrobody (argc argv)
                 (let in-file (p-get-in))
                 (p-set-in (p-stdin))
                 (p-print "Enter number:\n")
                 (let result (p-read))
                 (p-set-in in-file)
                 result)))

; 9.
; TODO: this does not work because letv doesn't yet
(let for
  '(()
    (p-macrobody (argc argv)
                 (let _lower (eval-arg argv 1))
                 (let _upper (eval-arg argv 2))
                 (let-mut _x _lower)
                 (p-while (s<= _x _upper)
                    (letv (eval-arg argv 0) _x)
                    (eval-arg argv 3)
                    (c:+= _x 1))
                 ())))
(for x 1 5 (print x))

; 11.
; FIXME: let scope is not root
(let load
  '(()
    (p-macrobody (argc argv)
                 (let _file (p-fopen (eval-arg argv 0) "r"))
                 (let _old-in (p-get-in))
                 (p-set-in _file)
                 (let sexp ())
                 (while (p-not (p-is-eof sexp))
                        (p-eval-here-then-there sexp (p-root-scope))
                        (c:= 'sexp (p-read)))
                 (p-set-in _old-in)
                 (p-fclose _file)
                 ())))

; tests:
(define test (actual expected call)
  (if (= expected actual)
    ()
    (begin (print "On call: ")
           (print call)
           (print " Expected value: ")
           (print expected)
           (print " Actual value: ")
           (print actual))))

; 1:
(test (sigma 1 5) 15 "(sigma 1 5)")
(test (sigma 1 10) 55 "(sigma 1 10)")
(test (sigma 5 5) 5 "(sigma 5 5)")
(test (sigma 5 3) 0 "(sigma 5 3)")

; 2:
(test (exp 2 0) 1 "(exp 2 0)")
(test (exp 2 10) 1024 "(exp 2 10)")
(test (exp 3 4) 81 "(exp 3 4)")
(test (exp 5 1) 5 "(exp 5 1)")

; 3:
(test (choose 5 0) 1 "(choose 5 0)")
(test (choose 5 5) 1 "(choose 5 5)")
(test (choose 5 2) 10 "(choose 5 2)")
(test (choose 6 3) 20 "(choose 6 3)")

; 4:
(test (fib 0) 0 "(fib 0)")
(test (fib 1) 1 "(fib 1)")
(test (fib 2) 1 "(fib 2)")
(test (fib 3) 2 "(fib 3)")
(test (fib 4) 3 "(fib 4)")
(test (fib 5) 5 "(fib 5)")
(test (fib 6) 8 "(fib 6)")

; 5:
(test (prime 1) 0 "(prime 1)")
(test (prime 2) 1 "(prime 2)")
(test (prime 3) 1 "(prime 3)")
(test (prime 4) 0 "(prime 4)")
(test (prime 5) 1 "(prime 5)")
(test (prime 6) 0 "(prime 6)")
(test (nthprime 1) 2 "(nthprime 1)")
(test (nthprime 2) 3 "(nthprime 2)")
(test (nthprime 3) 5 "(nthprime 3)")
(test (sumprimes 1) 2 "(sumprimes 1)")
(test (sumprimes 2) 5 "(sumprimes 2)")
(test (sumprimes 3) 10 "(sumprimes 3)")
(test (relprime 2 3) 1 "(relprime 2 3)")
(test (relprime 2 4) 0 "(relprime 2 4)")
(test (relprime 1 3) 0 "(relprime 1 3)")
(test (relprime 6 7) 1 "(relprime 6 7)")
(test (relprime 6 15) 0 "(relprime 6 15)")

; 6:
(test (binary 12) 1100 "(binary 12)")
(test (binary 255) 11111111 "(binary 255)")
(test (binary 256) 100000000 "(binary 256)")
(test (binary 0) 0 "(binary 0)")
(test (binary 1) 1 "(binary 1)")
(test (binary 2) 10 "(binary 2)")
(test (binary 3) 11 "(binary 3)")
(test (binary 4) 100 "(binary 4)")
(test (binary 5) 101 "(binary 5)")

; enter a REPL
(let i 0)
; (while 0
(while 1
  (p-set-in (p-stdin))
  (p-print "-> ")
  (let in-sexp (p-read))
  (p-print (p-format i))
  (c:+= i 1)
  (p-print ": ")
  (p-print (p-format (eval-2 in-sexp)))
  (p-print "\n"))
