(import _process)

(defn spawn [args &keys {:cmd cmd :gc-signal gc-signal :redirects redirects :env env}]
  
  (default cmd (get args 0))
  (default gc-signal :SIGTERM)
  (default redirects [])
  (when (nil? cmd)
    (error "args must be present or you must specify :cmd"))

  (def finish @[])

  (defn coerce-file [f]
    (cond 
      (= :discard f)
        (do 
          (def discardf (file/open "/dev/null" :wb))
          (unless discardf (error "unable to open discard file"))
          (array/push finish (fn [] (file/close discardf)))
          discardf)
      f))

  (let [redirects (map (fn [r] (map coerce-file r)) redirects)]
    (def proc (_process/primitive-spawn cmd args gc-signal redirects env))
    (each f finish (f))
    proc))

(def wait _process/wait)

(def signal _process/signal)

(defn run [args &keys {:cmd cmd :gc-signal gc-signal :redirects redirects :env env}]
  (default redirects [])
  (def finish @[])

  (defn coerce-file [f]
    (cond 
      (or (buffer? f) (string? f))
        (do
          (def tmpf (file/temp))
          (array/push finish
            (fn []
              (when (buffer? f)
                (file/seek tmpf :set 0)
                (file/read tmpf :all f))
              (file/close tmpf)))
          tmpf)
      f))

  (let [redirects (map (fn [r] (map coerce-file r)) redirects)]
    (def p (spawn args :cmd cmd :gc-signal gc-signal :redirects redirects :env env))
    (def exit-code (wait p))
    (each f finish (f))
    exit-code))
