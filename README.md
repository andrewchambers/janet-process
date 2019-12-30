# janet-proctools

A collection of modules for dealing with processes.

# process module

## Quick example

```
(import process)

(def redis-process 
  (process/spawn ["redis-server"] :redirects [[stderr stdout] [stdout (file/open "out.log" :wb)]]))

(process/signal redis-process :SIGTERM)

(process/wait redis-process)

(def buf (buffer/new 0))
(process/run ["echo" "hello"] :redirects [[stdout buf] [stderr :discard]])
```

# sh module

## Quick example

```
(import sh)

# raise an error on failure.
(sh/$ ["touch" "foo.txt"])

# raise an error on failure, return command output.
(sh/$$ ["echo" "hello world!"])
@"hello world!\n"

# return true or false depending on process success.
(when (sh/$? ["true"])
  (print "cool!"))

```
