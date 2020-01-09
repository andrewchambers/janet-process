# janet-process

A janet module for running child processes.

## Quick example

```
(import process)

(def redis-process 
  (process/spawn ["redis-server"] :redirects [[stderr stdout] [stdout (file/open "out.log" :wb)]]))

(process/signal redis-process :SIGTERM)

(process/wait redis-process)

(def buf (buffer/new 0))
(process/run ["echo" "hello"] :redirects [[stdout buf] [stderr :null]])
```

