(p-print "4")
(p-print "\n")
(p-print
  (p-format
    (p-if (p-sgt 1 0)
      3
      '( p-print  ( p-format      "\t")))))
(p-print "\n")
