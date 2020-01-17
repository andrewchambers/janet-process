(import process)

(do 
  (unless
    (zero? (process/run ["true"] :cmd "true" :redirects [[stdin :null] [stdout :null] [stderr :null]]))
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

(do 
  (def out (buffer/new 0))
  (unless (zero? (process/run ["pwd"] :redirects [[stderr out] [stdout stderr]] :start-dir "/tmp"))
    (error "process failed"))
  (unless (= "/tmp\n" (string out))
    (error "expected /tmp start dir")))

(do 
  (def out (buffer/new 0))
  (unless (zero? (process/run ["pwd"] :redirects [[stderr out] [stdout stderr]] :start-dir "/tmp"))
    (error "process failed"))
  (unless (= "/tmp\n" (string out))
    (error "expected /tmp start dir")))

(do 
  (def out (buffer/new 0))
  (unless (zero? (process/run ["cat"] :redirects [[stdout out] [stdin "hello world"]]))
    (error "process failed"))
  (unless (= "hello world" (string out))
    (error "output differs")))

# methods
(do 
  (def p
    (with [p (process/spawn ["sleep" "10"])]
      (os/sleep 0.1)
      (def pid (in p :pid))
      (unless (nil? (p :exit-code))
        (error "exit code shouldn't be set yet."))
      p))
  (when (nil? (p :exit-code))
    (error "exit-code should not be nil")))

# garbage collection shutdown.
(var v (process/spawn ["sleep" "10"] :gc-signal :SIGKILL))
(os/sleep 0.1)
(set v nil)
(gccollect)

# test :env
(do 
  (def env (merge (os/environ) {"PROC_TEST_FOOBAR_VAL" "BAR_BAZ"}))
  (def out (buffer/new 0))
  (unless
    (zero? (process/run ["sh" "-c" "echo $PROC_TEST_FOOBAR_VAL"] :env env :redirects [[stdout out]]))
    (error "process failed"))
  (unless (= "BAR_BAZ\n" (string out))
    (error "output differs")))
