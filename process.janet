(import _process)

(defn spawn
  ``
  spawn creates a forked child process and returns the process.

  If :cmd is specified, :cmd is executed as (get args 0)
  For example, (process/spawn ["sh"] :cmd "bash") executes
  bash as sh, causing it to run in POSIX shell mode.

  :close-signal is the singal sent to process during garbage collection.
  It can be :SIGTERM, :SIGKILL, etc, ...
  Refer to `man 7 signal` for a list of signals.
  The default signal is SIGTERM to allow processes to gracefully exit,
  however this means processes may cause delays during garbage collection
  if they do not exit promptly. Instead it is better to manually manage
  the life of a process.

  :env is a struct or a table of environment variables.
  You can pass (os/environ) to it.

  :start-dir is the directory where the command is executed.

  :redirects is a list of redirections. Each redirection is [f1 f2].
  Both f1 and f2 can be a file object or :null.
  stdin, stdout, and stderr are built-in file objects in janet.
  dup2(f2, f1) is called for each redirection in order.
  :null and janet buffers are converted to file objects before
  being passed to dup2. Refer to `man dup2` for more information.
  Order of redirections in :redirects is important because
  [[stdout :null] [stderr stdout]] is different from
  [[stderr stdout] [stdout :null]].
  If you want to feed stdin with something, try [stdin something].

  After a process is spawned, the returned object is
  queried for :pid and :exit-code, using the 'get' builtin.
  The returned object also has a :close method and is compatible with
  'with' forms.
  ``
  [args &keys {:cmd cmd
               :close-signal close-signal
               :redirects redirects
               :env env
               :start-dir start-dir}]
  (default cmd (get args 0))
  (default redirects [])
  (when (nil? cmd)
    (error "args must be present or you must specify :cmd"))

  (def finish @[])

  (defn coerce-file [f]
    (cond 
      (or (= :discard f) (= :null f)) # we could deprecate discard eventually
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
    (def proc (_process/primitive-spawn cmd args close-signal redirects env start-dir))
    (each f finish (f))
    proc))

(defn fork
  ``
    Fork the current process, returning nil in the child,
    or a process object in the parent.

    N.B. Extreme care must be taken when using fork. There is no way to prevent
    the janet garbage collector from running object destructors
    in both vm's after the fork. If active destructors are not safe to
    run twice, it make cause unexpected behavior. An example of this
    would be corrupting an sqlite3 database, as it is open in two processes
    after the fork.

    Accepts the following kwargs:

    :close-signal See spawn for details.
  ``
  [&keys {:close-signal close-signal}]
  
  (_process/primitive-fork close-signal))

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
  It accepts the same arguments that process/spawn does, with the addition
  that it accepts redirects into buffers to save command output.

  An example of saving stdout in a buffer:
  (def buf @"")
  (process/run ["cmd"] :redirects [[stdout buf]])
  ``
  [args &keys {:cmd cmd
               :close-signal close-signal
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
                  :cmd cmd :close-signal close-signal
                  :redirects redirects :env env :start-dir start-dir))
    (def exit-code (wait p))
    (each f finish (f))
    exit-code))
