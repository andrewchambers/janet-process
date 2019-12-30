(import process)

(do 
  (unless
    (zero? (process/run ["true"] :cmd "true" :redirects [[stdin :discard] [stdout :discard] [stderr :discard]]))
    (error "process failed")))

(do 
  (def out (buffer/new 0))
  (unless
    (zero? (process/run ["echo" "hello"] :redirects [[stdout out]]))
    (error "process failed"))
  (unless (= "hello\n" (string out))
    (error "output differs")))

(do 
  (def out (buffer/new 0))
  (unless (zero? (process/run ["echo" "hello"] :redirects [[stderr out] [stdout stderr]]))
    (error "process failed"))
  (unless (= "hello\n" (string out))
    (error "output differs")))

(var v (process/spawn ["sleep" "60"]))
(set v nil)
(gccollect)