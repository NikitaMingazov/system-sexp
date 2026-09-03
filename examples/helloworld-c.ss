(defstruct vec2
  (int x)
  (int y))

(defmacro cat (argc argv)
  (let length 0)
  ; (foreach (arr-iter argc argv)
  (for i 0 argc
    (s:+= length (p-strlen (eval-arg argv i))))
  (let formatted (p-uninit-str length))
  (for i 0 argc
    (p-memcpy formatted
              (eval-arg argv i)
              (p-strlen (eval-arg argv i))))
  formatted)

(defmacro defstruct (argc argv)
  (let memberlen 0)
  (for i 2 argc
    (s:+= memberlen (u+ 4 ; \t_ _;\n
                        (u+ (p-strlen (type-str (nth argv i)))
                            (p-strlen (value (nth argv i)))))))
  (let members (p-uninit-str memberlen))
  (let idx 0)
  (for i 2 argc
    (begin (p-memcpy (u+ members idx) "\t" 1)
           (s:+= idx 1)
           ; TODO: add an append method into lps that copies lps into another but does not allocate (for use on uninit lps)
           (p-memcpy (u+ members idx)
                     (type-str (nth argv i))
                     (p-strlen (type-str (nth argv i))))
           (s:+= idx (p-strlen (type-str (nth argv i))))
           (p-memcpy (u+ members idx) " " 1)
           (s:+= idx 1)
           (p-memcpy (u+ members idx)
                     (value (nth argv i))
                     (p-strlen (value (nth argv i))))
           (p-memcpy (u+ members idx) ";\n" 2)
           (s:+= idx 2)))
  (let result (cat "typedef struct " name " {\n" members "} " name ";\n"))
  (p-str-free members)
  result)

; typedef struct vec2 {
;     int x;
;     int y;
; } vec2;

(defun vec2_mul ((vec2 a) (vec2 b)) vec2
  (defvar result (vec2 ((sget x a) (sget y a))))
  (*= (sget result x) (sget b x))
  (*= (sget result y) (sget b y))
  (return result))

; vec2 vec2_mul(vec2 a, vec2 b) {
;     vec2 result = (vec2) { a.x, a.y };
;     result.x *= b.x;
;     result.y *= b.y;
;     return result;
; }

(defun pow ((int x) (int k)) int
  (defvar result 1)
  (for (defvar i 0) (< i k) (++ i)
    (*= result x))
  (return result))

; int pow(int x, int k) {
;     int result = 1;
;     for (int i = 0; i < k; ++i)
;         result *= x;
;     return result;
; }

(include<> "stdio.h")

(defun main ((int argc) (char** argv)) int
  (puts "Hello, World!")
  (return 0))
