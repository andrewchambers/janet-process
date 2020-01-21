(import _process)

(defn spawn [args &keys {:cmd cmd
                         :gc-signal gc-signal
                         :redirects redirects
                         :env env
                         :start-dir start-dir}]
  
  (default cmd (get args 0))
  (default gc-signal :SIGTERM)
  (default redirects [])
  (when (nil? cmd)
    (error "args must be present or you must specify :cmd"))

  (def finish @[])

  (defn coerce-file [f]
    (cond 
      (or (= :discard f) (= :null f)) # we could depcrecate discard eventually
        (do 
          (def discardf (file/open "/dev/null" :wb))
          (unless discardf (error "unable to open discard file"))
          (array/push finish (fn [] (file/close discardf)))
          discardf)
      f))

  (defn coerce-input-file [f]
    (cond 
      (or (buffer? f) (string? f))
      (do
        (def tmpf (file/temp))
        (file/write tmpf f)
        (file/flush tmpf)
        (file/seek tmpf :set 0)
        (array/push finish
          (fn []
            (file/close tmpf)))
        tmpf)
      (coerce-file f)))

  (defn coerce-redirect [[f1 f2]]
    (cond
      (= f1 stdin)
      [f1 (coerce-input-file f2)]
      [(coerce-file f1) (coerce-file f2)]))

  (let [redirects (map coerce-redirect redirects)]
    (def proc (_process/primitive-spawn cmd args gc-signal redirects env start-dir))
    (each f finish (f))
    proc))

(def pipe _process/pipe)

(def wait _process/wait)

(def signal _process/signal)

(defn run [args &keys {:cmd cmd
                       :gc-signal gc-signal
                       :redirects redirects
                       :env env
                       :start-dir start-dir}]
  (default redirects [])
  (def finish @[])

  (defn coerce-file [f]
    (cond 
      (buffer? f)
      (do
        (def tmpf (file/temp))
        (array/push finish
          (fn []
            (file/seek tmpf :set 0)
            (file/read tmpf :all f)
            (file/close tmpf)))
        tmpf)
      f))

  (defn coerce-redirect [[f1 f2]]
    (cond
      (= f1 stdin)
      [f1 f2] # It seems wrong to handle stdin specially,
              # but I'm not sure of a way around the issue that 
              # if we redirect stdout/stderr to a buffer we want to append to the buffer.
              # if we redirect stdin to a buffer, we want to write the buffer.
      [(coerce-file f1) (coerce-file f2)]))

  (let [redirects (map coerce-redirect redirects)]
    (def p (spawn args :cmd cmd :gc-signal gc-signal :redirects redirects :env env :start-dir start-dir))
    (def exit-code (wait p))
    (each f finish (f))
    exit-code))
