#define _XOPEN_SOURCE 500
#include <errno.h>
#include <fcntl.h>
#include <janet.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

typedef struct {
  pid_t pid;
  int gc_signal;
  int exited;
  int wstatus;
  char *cmd;
  char **argv;
  char **environ;
  JanetBuffer *out_buffers[2];
} Process;

/*
   Get a process exit code, the process must have had process_wait called.
   Returns -1 and sets errno on error, otherwise returns the exit code.
*/
static int process_exit_code(Process *p) {
  if (!p->exited) {
    errno = EINVAL;
    return -1;
  }

  int exit_code = 0;

  if (WIFEXITED(p->wstatus)) {
    exit_code = WEXITSTATUS(p->wstatus);
  } else if (WIFSIGNALED(p->wstatus)) {
    // Should this be a function of the signal?
    exit_code = 129;
  } else {
    /* This should be unreachable afaik */
    errno = EINVAL;
    return -1;
  }

  return exit_code;
}

/*
   Returns -1 and sets errno on error, otherwise returns the process exit code.
*/
static int process_wait(Process *p) {
  if (p->exited || p->pid == -1)
    return process_exit_code(p);

  int err;

  do {
    err = waitpid(p->pid, &p->wstatus, 0);
  } while (err < 0 && errno == EINTR);

  if (err < 0) {
    return -1;
  }

  p->exited = 1;
  return process_exit_code(p);
}

static int process_signal(Process *p, int sig) {
  int err;

  if (p->exited || p->pid == -1)
    return 0;

  do {
    err = kill(p->pid, sig);
  } while (err < 0 && errno == EINTR);

  if (err < 0)
    return -1;

  return 0;
}

static int process_gc(void *ptr, size_t s) {
  (void)s;
  int err;

  Process *p = (Process *)ptr;
  if (!p->exited && p->pid != -1) {
    do {
      err = kill(p->pid, p->gc_signal);
    } while (err < 0 && errno == EINTR);
    if (process_wait(p) < 0) {
      /* Not much we can do here. */
      p->exited = 1;
    }
  }
  if (p->cmd)
    free(p->cmd);
  if (p->argv) {
    size_t k = 0;
    while (p->argv[k]) {
      free(p->argv[k]);
      k++;
    }
    free(p->argv);
  }

  if (p->environ) {
    size_t k = 0;
    while (p->environ[k]) {
      free(p->environ[k]);
      k++;
    }
    free(p->environ);
  }
  return 0;
}

static Janet process_method_close(int32_t argc, Janet *argv);

static JanetMethod process_methods[] = {
    {"close", process_method_close}, /* So processes can be used with 'with' */
    {NULL, NULL}};

static int process_get(void *ptr, Janet key, Janet *out) {
  Process *p = (Process *)ptr;

  if (!janet_checktype(key, JANET_KEYWORD))
    return 0;

  if (janet_keyeq(key, "pid")) {
    *out = janet_wrap_integer(p->pid);
    return 1;
  }

  if (janet_keyeq(key, "exit-code")) {
    *out = janet_wrap_integer(process_exit_code(p));
    return 1;
  }

  return janet_getmethod(janet_unwrap_keyword(key), process_methods, out);
}

static const JanetAbstractType process_type = {
    "sh.process", process_gc, NULL, process_get, NULL,
    NULL,         NULL,       NULL, NULL,        NULL};

#define OUT_OF_MEMORY                                                          \
  do {                                                                         \
    janet_panic("out of memory");                                              \
  } while (0)

static int xclose(int fd) {
  int err;
  do {
    err = close(fd);
  } while (err < 0 && errno == EINTR);
  return err;
}

static int janet_to_signal(Janet j) {
  if (janet_keyeq(j, "SIGKILL")) {
    return SIGKILL;
  } else if (janet_keyeq(j, "SIGTERM")) {
    return SIGTERM;
  } else {
    return -1;
  }
}

static int reset_all_signal_handlers(void) {
#define RESET(S)                                                               \
  do {                                                                         \
    if (signal(S, SIG_DFL) == SIG_ERR)                                         \
      return -1;                                                               \
  } while (0)

  RESET(SIGABRT);
  RESET(SIGALRM);
  RESET(SIGBUS);
  RESET(SIGCHLD);
  RESET(SIGCONT);
  RESET(SIGFPE);
  RESET(SIGHUP);
  RESET(SIGILL);
  RESET(SIGINT);
  RESET(SIGPIPE);
  RESET(SIGPOLL);
  RESET(SIGPROF);
  RESET(SIGQUIT);
  RESET(SIGSEGV);
  RESET(SIGSTKFLT);
  RESET(SIGTSTP);
  RESET(SIGSYS);
  RESET(SIGTERM);
  RESET(SIGTRAP);
  RESET(SIGTTIN);
  RESET(SIGTTOU);
  RESET(SIGURG);
  RESET(SIGUSR1);
  RESET(SIGUSR2);
  RESET(SIGVTALRM);
  RESET(SIGXCPU);
  RESET(SIGXFSZ);
  RESET(SIGWINCH);
#undef RESET
  return 0;
}

static Janet jprimitive_spawn(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 5);

  Process *p = (Process *)janet_abstract(&process_type, sizeof(Process));

  p->gc_signal = SIGKILL;
  p->pid = -1;
  p->exited = 1;
  p->wstatus = 0;
  p->cmd = NULL;
  p->argv = NULL;
  p->environ = NULL;
  p->out_buffers[0] = NULL;
  p->out_buffers[1] = NULL;

  /* Janet strings are zero terminated so this is ok. */
  p->cmd = strdup((const char *)janet_getstring(argv, 0));
  if (!p->cmd)
    OUT_OF_MEMORY;
  JanetView args = janet_getindexed(argv, 1);

  p->argv = calloc(args.len + 1, sizeof(char *));
  if (!p->argv)
    OUT_OF_MEMORY;

  for (size_t i = 0; i < (size_t)args.len; i++) {
    p->argv[i] = strdup(janet_getcstring(&args.items[i], 0));
    if (!p->argv[i])
      OUT_OF_MEMORY;
  }

  Janet gc_signal = argv[2];
  if (!janet_checktype(gc_signal, JANET_NIL)) {
    int gc_signal_int = janet_to_signal(gc_signal);
    if (gc_signal_int == -1)
      janet_panic("invalid value for :gc-signal");

    p->gc_signal = gc_signal_int;
  }

  JanetView redirects = janet_getindexed(argv, 3);

  for (int i = 0; i < redirects.len; i++) {
    Janet t = redirects.items[i];

    if (!janet_checktype(t, JANET_TUPLE) && !janet_checktype(t, JANET_ARRAY))
      janet_panic("redirects must be tuples or arrays");

    JanetView r = janet_getindexed(&t, 0);
    if (r.len != 2)
      janet_panic("redirects must be two elements");

    for (int j = 0; j < 2; j++) {
      if (!janet_checkfile(r.items[j]))
        janet_panic("redirects must be files");
    }
  }

  if (!janet_checktype(argv[4], JANET_NIL)) {

    JanetDictView env = janet_getdictionary(argv, 4);

    p->environ = calloc((env.len + 1), sizeof(char *));
    if (!p->environ)
      OUT_OF_MEMORY;

    int32_t j = 0;
    for (int32_t i = 0; i < env.cap; i++) {
      const JanetKV *kv = env.kvs + i;
      if (janet_checktype(kv->key, JANET_NIL))
        continue;
      if (!janet_checktype(kv->key, JANET_STRING))
        janet_panic("environ key is not a string");
      if (!janet_checktype(kv->value, JANET_STRING))
        janet_panic("environ value is not a string");
      const uint8_t *keys = janet_unwrap_string(kv->key);
      const uint8_t *vals = janet_unwrap_string(kv->value);
      size_t klen = janet_string_length(keys);
      size_t vlen = janet_string_length(vals);
      if (strlen((char *)keys) != klen)
        janet_panic("environ keys cannot have embedded nulls");
      if (strlen((char *)vals) != vlen)
        janet_panic("environ values cannot have embedded nulls");
      char *envitem = malloc(klen + vlen + 2);
      if (!envitem)
        OUT_OF_MEMORY;
      memcpy(envitem, keys, klen);
      envitem[klen] = '=';
      memcpy(envitem + klen + 1, vals, vlen);
      envitem[klen + vlen + 1] = 0;
      p->environ[j++] = envitem;
    }
    p->environ[j] = NULL;
  }

  sigset_t all_signals_mask, old_block_mask;

  if (sigfillset(&all_signals_mask) != 0)
    janet_panic("unable to configure signal mask");

  if (sigprocmask(SIG_SETMASK, &all_signals_mask, &old_block_mask) != 0)
    janet_panic("unable to mask signals");

  pid_t pid = fork();

  if (pid != 0) {
    /* renable signals if we aren't the child. */
    if (sigprocmask(SIG_SETMASK, &old_block_mask, NULL) != 0) {
      /* If we can't restore the signal mask, we broke the whole process.
         all we can do is abort...
       */
      abort();
    }
  }

  if (pid < 0) {
    janet_panic("fork failed");
  } else if (pid) {
    /* Parent */
    p->exited = 0;
    p->pid = pid;
  } else {
    /* Child */
    int err;

    if (reset_all_signal_handlers() < 0) {
      fprintf(stderr, "child unable to reset signal handlers, aborting\n");
      exit(1);
    }

    /* now we have reset all the signal handlers, we can unblock them in the
     * child. */
    if (sigprocmask(SIG_UNBLOCK, &all_signals_mask, NULL) != 0) {
      /* If we can't restore the signal mask, we broke the whole process.
         all we can do is abort...
       */
      fprintf(stderr, "child unable to unblock signal handlers, aborting\n");
      exit(1);
    }

    for (int i = 0; i < redirects.len; i++) {
      Janet t = redirects.items[i];

      JanetView redir = janet_getindexed(&t, 0);
      int f1flags, f2flags;
      FILE *f1 = janet_unwrapfile(redir.items[0], &f1flags);
      FILE *f2 = janet_unwrapfile(redir.items[1], &f2flags);

      if (f1flags & JANET_FILE_CLOSED || f2flags & JANET_FILE_CLOSED) {
        fprintf(stderr, "redirect file already closed, aborting\n");
        exit(1);
      }

      do {
        err = dup2(fileno(f2), fileno(f1));
      } while (err < 0 && errno == EINTR);
      if (err < 0) {
        perror("dup2");
        exit(1);
      }

      if (!(f2flags & JANET_FILE_NOT_CLOSEABLE))
        if (xclose(fileno(f2)) < 0) {
          perror("close");
          exit(1);
        }
    }

    if (p->environ)
      environ = p->environ;

    execvp(p->cmd, p->argv);
    perror("execve");
    exit(1);
  }

  return janet_wrap_abstract(p);
}

static Janet jwait(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);
  Process *p = (Process *)janet_getabstract(argv, 0, &process_type);

  if (p->exited)
    janet_panic("wait already called on process");

  int exit_code = process_wait(p);
  if (exit_code < 0)
    janet_panicf("error waiting for process - %s", strerror(errno));

  return janet_wrap_integer(exit_code);
}

static Janet jsignal(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);
  Process *p = (Process *)janet_getabstract(argv, 0, &process_type);
  int sig = janet_to_signal(argv[1]);
  if (sig == -1)
    janet_panic("invalid signal");

  int rc = process_signal(p, sig);
  if (rc < 0)
    janet_panicf("unable to signal process - %s", strerror(errno));

  return janet_wrap_nil();
}

static Janet process_method_close(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);
  Process *p = (Process *)janet_getabstract(argv, 0, &process_type);

  if (p->exited)
    return janet_wrap_nil();

  int rc;

  rc = process_signal(p, p->gc_signal);
  if (rc < 0)
    janet_panicf("unable to signal process - %s", strerror(errno));

  rc = process_wait(p);
  if (rc < 0)
    janet_panicf("unable to wait for process - %s", strerror(errno));

  return janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"primitive-spawn", jprimitive_spawn, "(process/primitive-spawn spec)\n\n"},
    {"signal", jsignal, "(process/signal p sig)\n\n"},
    {"wait", jwait, "(process/wait p)\n\n"},
    {NULL, NULL, NULL}};

JANET_MODULE_ENTRY(JanetTable *env) { janet_cfuns(env, "_process", cfuns); }
