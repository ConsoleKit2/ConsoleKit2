/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/kd.h>
#endif

#ifdef HAVE_GETPEERUCRED
#include <ucred.h>
#endif

#include "ck-sysdeps.h"

/* Adapted from dbus-sysdeps-unix.c:_dbus_read_credentials_socket() */
gboolean
ck_get_socket_peer_credentials   (int      socket_fd,
                                  pid_t   *pid,
                                  uid_t   *uid,
                                  GError **error)
{
        gboolean ret;
        uid_t    uid_read;
        pid_t    pid_read;

        pid_read = -1;
        uid_read = -1;
        ret = FALSE;

#ifdef SO_PEERCRED
        struct ucred cr;
        socklen_t    cr_len;

        cr_len = sizeof (cr);

        if (getsockopt (socket_fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len) == 0 && cr_len == sizeof (cr)) {
                pid_read = cr.pid;
                uid_read = cr.uid;
                ret = TRUE;
        } else {
                g_warning ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
                           cr_len,
                           (int) sizeof (cr),
                           g_strerror (errno));
        }
#elif defined(HAVE_GETPEERUCRED)
        ucred_t *ucred;

        ucred = NULL;
        if (getpeerucred (socket_fd, &ucred) == 0) {
                pid_read = ucred_getpid (ucred);
                uid_read = ucred_geteuid (ucred);
                ret = TRUE;
        } else {
                g_warning ("Failed to getpeerucred() credentials: %s\n",
                           g_strerror (errno));
        }
        if (ucred != NULL) {
                ucred_free (ucred);
        }
#else /* !SO_PEERCRED && !HAVE_GETPEERUCRED */
        g_warning ("Socket credentials not supported on this OS\n");
#endif

        if (pid != NULL) {
                *pid = pid_read;
        }

        if (uid != NULL) {
                *uid = uid_read;
        }

        return ret;
}


/*
 * getfd.c
 *
 * Get an fd for use with kbd/console ioctls.
 * We try several things because opening /dev/console will fail
 * if someone else used X (which does a chown on /dev/console).
 */

gboolean
ck_fd_is_a_console (int fd)
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

        if (! ck_fd_is_a_console (fd)) {
                close (fd);
                fd = -1;
        }

        return fd;
}

int
ck_get_a_console_fd (void)
{
        int fd;

        fd = -1;

#ifdef __sun
	/* On Solaris, first try Sun VT device. */
        fd = open_a_console ("/dev/vt/active");
        if (fd >= 0) {
                goto done;
        }
        fd = open_a_console ("/dev/vt/0");
        if (fd >= 0) {
                goto done;
        }
#endif

#ifdef _PATH_TTY
        fd = open_a_console (_PATH_TTY);
        if (fd >= 0) {
                goto done;
        }
#endif

        fd = open_a_console ("/dev/tty");
        if (fd >= 0) {
                goto done;
        }

#ifdef _PATH_CONSOLE
        fd = open_a_console (_PATH_CONSOLE);
        if (fd >= 0) {
                goto done;
        }
#endif

        fd = open_a_console ("/dev/console");
        if (fd >= 0) {
                goto done;
        }

        for (fd = 0; fd < 3; fd++) {
                if (ck_fd_is_a_console (fd)) {
                        goto done;
                }
        }
 done:
        return fd;
}

gboolean
ck_is_root_user (void)
{
#ifndef G_OS_WIN32
        uid_t ruid, euid, suid; /* Real, effective and saved user ID's */
        gid_t rgid, egid, sgid; /* Real, effective and saved group ID's */

#ifdef HAVE_GETRESUID
        if (getresuid (&ruid, &euid, &suid) != 0 ||
            getresgid (&rgid, &egid, &sgid) != 0)
#endif /* HAVE_GETRESUID */
                {
                        suid = ruid = getuid ();
                        sgid = rgid = getgid ();
                        euid = geteuid ();
                        egid = getegid ();
                }

        if (ruid == 0) {
                return TRUE;
        }

#endif
        return FALSE;
}
