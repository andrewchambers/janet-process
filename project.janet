(declare-project
  :name "proctools"
  :author "Andrew Chambers"
  :license "MIT"
  :url "https://github.com/andrewchambers/janet-proctools"
  :repo "git+https://github.com/andrewchambers/janet-proctools.git")

(declare-native
  :name "_process"
  :source ["process/process.c"])

(declare-source
  :name "process"
  :source ["process/process.janet"])

(declare-source
  :name "sh"
  :source ["sh/sh.janet"])
