#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* https://stackoverflow.com/questions/32427793 */
#ifndef NSIG
#if defined(_SIGMAX)
#define NSIG (_SIGMAX + 1)
#elif defined(_NSIG)
#define NSIG _NSIG
#else
#error "don't know how many signals"
#endif
#endif

void reset_all_signal_handlers(void) {
    for (int sig = 1; sig < NSIG; sig++) {
        /* Ignore errors here */
        signal(sig, SIG_DFL);
    }
}

int xclose(int fd) {
  int err;
  do {
    err = close(fd);
  } while (err < 0 && errno == EINTR);
  return err;
}

int ensure_child_fds_from_are_closed_at_exec(int lowfd) {
#if defined(__FreeBSD__)
  closefrom(lowfd);
  return 0;
#else
  const char *path;
  DIR *dirp;
  struct dirent *dent;
  int ret = 0;

  /* Use /proc/self/fd (or /dev/fd on macOS) if it exists. */
#if defined(__APPLE__)
  path = "/dev/fd";
#else
  path = "/proc/self/fd";
#endif
  dirp = opendir(path);
  if (dirp == NULL)
    return -1;

  errno = 0;
  while ((dent = readdir(dirp)) != NULL) {
    int fd;

    fd = atoi(dent->d_name);
    if (fd < lowfd)
      continue;

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
      /* Not sure there is anything we can do... */
      ;
    }

    errno = 0;
  }

  /* readdir failed */
  if (errno)
    ret = -1;

  (void)closedir(dirp);

  return ret;
#endif
}
