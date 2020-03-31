#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <janet.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void reset_all_signal_handlers(void);
int xclose(int fd);
int ensure_child_fds_from_are_closed_at_exec(int lowfd);

extern char **environ;

typedef struct {
  pid_t pid;
  int close_signal;
  int exited;
  int wstatus;
} Process;

/*
   Get a process exit code, the process must have had process_wait called.
   Returns -1 and sets errno on error, otherwise returns the exit code.
*/
static int process_exit_code(Process *p) {
  if (!p->exited || p->pid == -1) {
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

static int process_wait(Process *p, int *exit, int flags) {
  int _exit = 0;
  if (!exit)
    exit = &_exit;

  if (p->pid == -1) {
    errno = EINVAL;
    return -1;
  }

  if (p->exited) {
    *exit = process_exit_code(p);
    return 0;
  }

  int err;

  do {
    err = waitpid(p->pid, &p->wstatus, flags);
  } while (err < 0 && errno == EINTR);

  if (err < 0)
    return -1;

  if ((flags & WNOHANG && err == 0)) {
    *exit = -1;
    return 0;
  }

  p->exited = 1;
  *exit = process_exit_code(p);
  return 0;
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
      err = kill(p->pid, p->close_signal);
    } while (err < 0 && errno == EINTR);
    if (process_wait(p, NULL, 0) < 0) {
      /* Not much we can do here. */
      p->exited = 1;
    }
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
    *out = (p->pid == -1) ? janet_wrap_nil() : janet_wrap_integer(p->pid);
    return 1;
  }

  if (janet_keyeq(key, "exit-code")) {
    int exit_code;

    if (process_wait(p, &exit_code, WNOHANG) != 0)
      janet_panicf("error checking exit status: %s", strerror(errno));

    *out = (exit_code == -1) ? janet_wrap_nil() : janet_wrap_integer(exit_code);
    return 1;
  }

  return janet_getmethod(janet_unwrap_keyword(key), process_methods, out);
}

static const JanetAbstractType process_type = {
    "process/process", process_gc, NULL, process_get, JANET_ATEND_GET};

#define OUT_OF_MEMORY                                                          \
  do {                                                                         \
    janet_panic("out of memory");                                              \
  } while (0)

static int janet_to_signal(Janet j) {
  if (janet_keyeq(j, "SIGKILL")) {
    return SIGKILL;
  } else if (janet_keyeq(j, "SIGTERM")) {
    return SIGTERM;
  } else if (janet_keyeq(j, "SIGINT")) {
    return SIGINT;
  } else if (janet_keyeq(j, "SIGHUP")) {
    return SIGHUP;
  } else {
    return -1;
  }
}

/* XXX we should be able to delete this if we upstream janet_scalloc */
static void *scratch_calloc(size_t nmemb, size_t size) {
  if (nmemb && size > (size_t)-1 / nmemb) {
    OUT_OF_MEMORY;
  }
  size_t n = nmemb * size;
  void *p = janet_smalloc(n);
  memset(p, 0, n);
  return p;
}

static Janet jprimitive_spawn(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 6);

  const char *pcmd;
  const char *pstartdir;
  char **pargv;
  char **penviron;

  Process *p = (Process *)janet_abstract(&process_type, sizeof(Process));

  p->close_signal = SIGKILL;
  p->pid = -1;
  p->exited = 1;
  p->wstatus = 0;

  pcmd = NULL;
  pstartdir = NULL;
  pargv = NULL;
  penviron = NULL;

  /* Janet strings are zero terminated so this is ok. */
  pcmd = (const char *)janet_getstring(argv, 0);
  JanetView args = janet_getindexed(argv, 1);

  pargv = scratch_calloc(args.len + 1, sizeof(char *));

  for (size_t i = 0; i < (size_t)args.len; i++) {
    pargv[i] = (char *)janet_getcstring(&args.items[i], 0);
  }

  Janet close_signal = argv[2];
  if (!janet_checktype(close_signal, JANET_NIL)) {
    int close_signal_int = janet_to_signal(close_signal);
    if (close_signal_int == -1)
      janet_panic("invalid value for :gc-signal");

    p->close_signal = close_signal_int;
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

    penviron = scratch_calloc((env.len + 1), sizeof(char *));

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
      char *envitem = janet_smalloc(klen + vlen + 2);
      if (!envitem)
        OUT_OF_MEMORY;
      memcpy(envitem, keys, klen);
      envitem[klen] = '=';
      memcpy(envitem + klen + 1, vals, vlen);
      envitem[klen + vlen + 1] = 0;
      penviron[j++] = envitem;
    }
  }

  if (!janet_checktype(argv[5], JANET_NIL)) {
    pstartdir = janet_getcstring(argv, 5);
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
         all we can do is abort or risk having the program in an undefined
         state.
       */
      abort();
    }

    /* Free the things we allocated in reverse order */
    if (penviron) {
      for (int i = 0; penviron[i]; i++) {
        janet_sfree(penviron[i]);
      }
      janet_sfree(penviron);
    }
    janet_sfree(pargv);
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

    reset_all_signal_handlers();

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

    if (ensure_child_fds_from_are_closed_at_exec(4) < 0) {
      fprintf(stderr, "unable to ensure fds will close, aborting\n");
      exit(1);
    }

    if (pstartdir)
      if (chdir(pstartdir) < 0) {
        perror("chdir");
        exit(1);
      }

    if (penviron)
      environ = penviron;

    execvp(pcmd, pargv);

    size_t exec_errmsg_sz = 128 + strlen(pcmd);
    char *exec_errmsg = malloc(exec_errmsg_sz);
    if(!exec_errmsg)
      abort();
    snprintf(exec_errmsg, exec_errmsg_sz, "exec %s failed", pcmd);
    perror(exec_errmsg);
    exit(1);
  }

  return janet_wrap_abstract(p);
}

static Janet jwait(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);
  Process *p = (Process *)janet_getabstract(argv, 0, &process_type);

  if (p->exited)
    janet_panic("wait already called on process");

  int exit_code;

  if (process_wait(p, &exit_code, 0) != 0)
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

  rc = process_signal(p, p->close_signal);
  if (rc < 0)
    janet_panicf("unable to signal process - %s", strerror(errno));

  rc = process_wait(p, NULL, 0);
  if (rc < 0)
    janet_panicf("unable to wait for process - %s", strerror(errno));

  return janet_wrap_nil();
}

static Janet jpipe(int32_t argc, Janet *argv) {
  (void)argv;
  janet_fixarity(argc, 0);

  int mypipe[2];
  if (pipe(mypipe) < 0)
    janet_panicf("unable to allocate pipe - %s", strerror(errno));

  FILE *p1 = fdopen(mypipe[0], "rb");
  FILE *p2 = fdopen(mypipe[1], "wb");
  if (!p1 || !p2)
    janet_panicf("unable to create file objects - %s", strerror(errno));

  Janet *t = janet_tuple_begin(2);
  t[0] = janet_makefile(p1, JANET_FILE_READ | JANET_FILE_BINARY);
  t[1] = janet_makefile(p2, JANET_FILE_WRITE | JANET_FILE_BINARY);
  return janet_wrap_tuple(janet_tuple_end(t));
}

static const JanetReg cfuns[] = {
    {"primitive-spawn", jprimitive_spawn, "(process/primitive-spawn spec)\n\n"},
    {"signal", jsignal, "(process/signal p sig)\n\n"},
    {"wait", jwait, "(process/wait p)\n\n"},
    {"pipe", jpipe, "(process/pipe)\n\n"},
    {NULL, NULL, NULL}};

JANET_MODULE_ENTRY(JanetTable *env) { janet_cfuns(env, "_process", cfuns); }
