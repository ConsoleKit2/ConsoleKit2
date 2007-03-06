/*
 * Adapted from kbd-1.12
 * License: GPL
 *
 */

#include "config.h"

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/kd.h>
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

    arg = 0;
    return (ioctl (fd, KDGKBTYPE, &arg) == 0
	    && ((arg == KB_101) || (arg == KB_84)));
}

static int
open_a_console (char *fnam)
{
    int fd;

    fd = open (fnam, O_RDONLY);
    if (fd < 0 && errno == EACCES)
      fd = open(fnam, O_WRONLY);
    if (fd < 0 || ! is_a_console (fd))
      return -1;
    return fd;
}

int getfd (void)
{
    int fd;

    fd = open_a_console (_PATH_TTY);
    if (fd >= 0)
      return fd;

    fd = open_a_console ("/dev/tty");
    if (fd >= 0)
      return fd;

    fd = open_a_console (_PATH_CONSOLE);
    if (fd >= 0)
      return fd;

    fd = open_a_console ("/dev/console");
    if (fd >= 0)
      return fd;

    for (fd = 0; fd < 3; fd++)
      if (is_a_console (fd))
	return fd;

    return -1;
}
