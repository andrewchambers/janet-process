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
```

# sh module

## Quick example

```
(import sh)

(sh/$ ["touch" "foo.txt"])

(sh/$$ ["echo" "hello world!"])
"hello world!\n"

(when (sh/$? ["false"])
  (print "cool!"))

(sh/$$ (sh/pipeline [["cat" "foo.txt"] ["sort" "-u"]]))
"a\nb\nc\n"
```