/*
 * Adapted from kbd-1.12
 * License: GPL
 *
 */

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/kd.h>
#endif
#include <sys/ioctl.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

/*
 * getfd.c
 *
 * Get an fd for use with kbd/console ioctls.
 * We try several things because opening /dev/console will fail
 * if someone else used X (which does a chown on /dev/console).
 */

static int
is_a_console (int fd)
{
    char arg;
    int  kb_ok;

    arg = 0;

#ifdef __linux__
    kb_ok = (ioctl (fd, KDGKBTYPE, &arg) == 0
             && ((arg == KB_101) || (arg == KB_84)));
#else
    kb_ok = 1;
#endif

    return (isatty (fd) && kb_ok);
}

static int
open_a_console (char *fnam)
{
    int fd;

    fd = open (fnam, O_RDONLY | O_NOCTTY);
    if (fd < 0 && errno == EACCES)
      fd = open (fnam, O_WRONLY | O_NOCTTY);

    if (fd < 0)
      return -1;

    if (! is_a_console (fd)) {
      close (fd);
      fd = -1;
    }

    return fd;
}

int getfd (void)
{
    int fd;

#ifdef _PATH_TTY
    fd = open_a_console (_PATH_TTY);
    if (fd >= 0)
      return fd;
#endif

    fd = open_a_console ("/dev/tty");
    if (fd >= 0)
      return fd;

#ifdef _PATH_CONSOLE
    fd = open_a_console (_PATH_CONSOLE);
    if (fd >= 0)
      return fd;
#endif

    fd = open_a_console ("/dev/console");
    if (fd >= 0)
      return fd;

    for (fd = 0; fd < 3; fd++)
      if (is_a_console (fd))
	return fd;

    return -1;
}
