#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int xclose(int fd) {
  int err;
  do {
    err = close(fd);
  } while (err < 0 && errno == EINTR);
  return err;
}

int ensure_child_fds_from_are_closed_at_exec(int lowfd) {
  const char *path;
  DIR *dirp;
  struct dirent *dent;
  int ret = 0;

  /* Use /proc/self/fd (or /dev/fd on FreeBSD) if it exists. */
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__APPLE__)
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
}