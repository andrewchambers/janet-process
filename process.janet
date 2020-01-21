(import _process)

(defn spawn
  ``
  spawn creates a forked child process and returns the process.

  If :cmd is specified, :cmd is executed as (get args 0)
  For example, (process/spawn ["sh"] :cmd "bash") executes
  bash as sh. When bash is executed as sh, it enters POSIX shell mode.

  :gc-signal is the singal sent to process during garbage collection.
  It can be :SIGTERM, :SIGKILL, etc, ...
  Refer to `man 7 signal` for a list of signals.

  :env is a struct or a table of environment variables.
  You can pass (os/environ) to it.

  :start-dir is the directory where the command is executed.

  :redirects is a list of redirections. Each redirection is [f1 f2].
  Both f1 and f2 can be :null, a janet buffer, or a file object.
  stdin, stdout, and stderr are built-in file objects in janet.
  If stdin is fed a file object, the launched process doesn't end
  until the file object is closed.
  dup2(f2, f1) is called for each redirection in order.
  :null and janet buffers are converted to file objects before
  being passed to dup2. Refer to `man dup2` for more information.
  Order of redirections in :redirects is important because
  [[stdout :null] [stderr stdout]] is different from
  [[stderr stdout] [stdout :null]].
  If you want to feed stdin with something, try [stdin something].
  ``
  [args &keys {:cmd cmd
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

(def pipe
  ``
  Returns [rb wb].
  rb is a readable binary file object.
  wb is a writable binary file object.
  You can read from rb what you write to wb.

  You can use this to pipe stdout of a process to stdin of another process.
  If you feed stdin of a process with rb, the process won't exit until
  rb is closed. Remember to close rb and wb somewhere.
  (defer) can guarantee that rb and wb are closed.

  Example Usage: (Don't expect this to work properly in real code.)
  (def [p1 p2] (process/pipe))
  (def [p3 p4] (process/pipe))
  (process/spawn ["xxx"] :redirects [[stdout p2]])
  (process/spawn ["xxx"] :redirects [[stdin p1] [stdout p4]])
  (file/read p3 :all)
  ``
  _process/pipe)

(def wait
  ``
  Wait until the process returned by process/spawn exits, and
  return the exit code of the process.

  Example Usage:
  (process/wait process)
  ``
  _process/wait)

(def signal
  ``
  Send a signal to the process returned by process/spawn.
  The signal can be :SIGTERM, :SIGKILL, etc, ...
  Refer to `man 7 signal` for a list of signals.

  Example Usage:
  (process/signal process signal)
  ``
  _process/signal)

(defn run
  ``
  This runs process in the foreground and returns exit code of the process.
  It accepts the same arguments that process/spawn does.
  ``
  [args &keys {:cmd cmd
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
    (def p (spawn args
                  :cmd cmd :gc-signal gc-signal
                  :redirects redirects :env env :start-dir start-dir))
    (def exit-code (wait p))
    (each f finish (f))
    exit-code))
