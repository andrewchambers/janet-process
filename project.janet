(declare-project
  :name "process"
  :description "A Janet module for running child processes."
  :author "Andrew Chambers"
  :license "MIT"
  :url "https://github.com/andrewchambers/janet-process"
  :repo "git+https://github.com/andrewchambers/janet-process.git")

(declare-native
  :name "_process"
  :source ["util.c" "process.c"])

(declare-source
  :name "process"
  :source ["process.janet"])

