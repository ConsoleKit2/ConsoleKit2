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
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <pwd.h>

#ifdef HAVE_LINUX_KD_H
#include <linux/kd.h>
#endif

#if defined(__NetBSD__) || defined(__OpenBSD__)
#include <dev/wscons/wsdisplay_usl_io.h>
#endif

#if defined(__NetBSD__)
#include <sys/un.h>
#endif

#ifdef HAVE_SYS_VT_H
#include <sys/vt.h>
#endif

#ifdef HAVE_SYS_CONSIO_H
#include <sys/consio.h>
#endif

#ifdef HAVE_GETPEERUCRED
#include <ucred.h>
#endif

#ifdef HAVE_KVM_H
#include <kvm.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#ifdef HAVE_SYS_USER_H
#include <sys/user.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "ck-sysdeps.h"

#ifndef ERROR
#define ERROR -1
#endif

/* Adapted from dbus-sysdeps-unix.c:_dbus_read_credentials_socket() */
gboolean
ck_get_socket_peer_credentials   (int      socket_fd,
                                  pid_t   *pid,
                                  uid_t   *uid,
                                  GError **error)
{
        gboolean ret = FALSE;
        uid_t    uid_read = 0;
        pid_t    pid_read = 0;

#ifdef SO_PEERCRED
#ifndef __OpenBSD__
        struct ucred cr;
#else
        struct sockpeercred cr;
#endif
        socklen_t    cr_len;

        pid_read = -1;
        uid_read = -1;

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
#elif defined(LOCAL_PEEREID)
        struct unpcbid cr;
        socklen_t      cr_len;

        cr_len = sizeof (cr);

        if (getsockopt (socket_fd, 0, LOCAL_PEEREID, &cr, &cr_len) == 0 && cr_len == sizeof (cr)) {
                pid_read = cr.unp_pid;
                uid_read = cr.unp_euid;
                ret = TRUE;
        } else {
                g_warning ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
                           cr_len,
                           (int) sizeof (cr),
                           g_strerror (errno));
        }
#elif defined(HAVE_GETPEEREID)
	gid_t dummy;
        char errbuf[_POSIX2_LINE_MAX];
        int cnt = 0;
        kvm_t* kd;
        struct kinfo_proc * prc;

        if (getpeereid (socket_fd, &uid_read, &dummy) == 0) {
                ret = TRUE;
        } else {
                g_warning ("Failed to getpeereid() credentials: %s\n",
                           g_strerror (errno));
        }
#if defined(__FreeBSD__)
        kd = kvm_openfiles (NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf);
        if (kd == NULL) {
                g_warning ("kvm_openfiles failed: %s", errbuf);
                return FALSE;
        }

        prc = kvm_getprocs (kd, KERN_PROC_UID, uid_read, &cnt);
        for (int i = 0; i < cnt; i++) {
                if(strncmp (prc[i].ki_comm, "Xorg", 4) == 0) {
                        pid_read = prc[i].ki_pid;
                        break;
                }
        }

        kvm_close(kd);
#endif /* __FreeBSD__ */
#if defined(__DragonFly__)
        kd = kvm_openfiles (NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf);
        if (kd == NULL) {
                g_warning ("kvm_openfiles failed: %s", errbuf);
                return FALSE;
        }

        prc = kvm_getprocs (kd, KERN_PROC_UID, uid_read, &cnt);
        for (int i = 0; i < cnt; i++) {
                if(strncmp (prc[i].kp_comm, "Xorg", 4) == 0) {
                        pid_read = prc[i].kp_pid;
                        break;
                }
        }

        kvm_close(kd);
#endif /* DragonFly */
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
ck_fd_is_a_console (int fd,
                    const gchar *fnam)
{
#if defined(__linux__) || defined(__NetBSD__) || defined(__OpenBSD__)
        struct vt_stat vts;
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (__DragonFly__)
        int vers;
#endif
        int  kb_ok;

        errno = 0;
#if defined(__linux__) || defined(__NetBSD__) || defined(__OpenBSD__)
        kb_ok = (ioctl (fd, VT_GETSTATE, &vts) == 0);
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (__DragonFly__)
        kb_ok = (ioctl (fd, CONS_GETVERS, &vers) == 0);
#else
        kb_ok = 1;
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (__DragonFly__)
        return ((isatty (fd) || g_strcmp0 (fnam, "/dev/consolectl") == 0) && kb_ok);
#else
        return (isatty (fd) && kb_ok);
#endif
}

int
ck_open_a_console (char *fnam)
{
        int fd;

#ifdef __linux__
again:
#endif /* __linux__ */

        fd = open (fnam, O_RDONLY | O_NOCTTY);
        if (fd < 0 && errno == EACCES)
                fd = open (fnam, O_WRONLY | O_NOCTTY);
#ifdef __linux__
	if (fd < 0 && errno == EIO) {
		/* Linux can return EIO if the tty is currently closing,
		 * which can happen if multiple processes are opening and
		 * closing the console in parallel.  Unfortunately it can
		 * also return EIO in more serious situations too (see
		 * https://bugs.launchpad.net/bugs/554172), but there isn't
		 * much we can do about that since we really need a console
		 * fd.
		 */
		struct timespec ts = { 0, 100000000 }; /* 0.1 seconds */
		nanosleep (&ts, NULL);
		goto again;
	}
#endif /* __linux__ */

        if (fd < 0)
                return -1;

        if (! ck_fd_is_a_console (fd, fnam)) {
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
        fd = ck_open_a_console ("/dev/vt/active");
        if (fd >= 0) {
                goto done;
        }
        fd = ck_open_a_console ("/dev/vt/0");
        if (fd >= 0) {
                goto done;
        }
#endif

#if defined(__NetBSD__)
        fd = ck_open_a_console ("/dev/ttyE0");
        if (fd >= 0) {
                goto done;
        }
#endif

#if defined(__OpenBSD__)
        fd = ck_open_a_console ("/dev/ttyC0");
        if (fd >= 0) {
                goto done;
        }
#endif


#ifdef _PATH_TTY
        fd = ck_open_a_console (_PATH_TTY);
        if (fd >= 0) {
                goto done;
        }
#endif

        fd = ck_open_a_console ("/dev/tty");
        if (fd >= 0) {
                goto done;
        }

	fd = ck_open_a_console ("/dev/tty0");
	if (fd >= 0) {
		goto done;
	}

#ifdef _PATH_CONSOLE
        fd = ck_open_a_console (_PATH_CONSOLE);
        if (fd >= 0) {
                goto done;
        }
#endif

        fd = ck_open_a_console ("/dev/console");
        if (fd >= 0) {
                goto done;
        }

        for (fd = 0; fd < 3; fd++) {
                if (ck_fd_is_a_console (fd, "")) {
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
        uid_t ruid;

#ifdef HAVE_GETRESUID
        uid_t euid, suid; /* Real, effective and saved user ID's */
        gid_t rgid, egid, sgid; /* Real, effective and saved group ID's */

        if (getresuid (&ruid, &euid, &suid) != 0 ||
            getresgid (&rgid, &egid, &sgid) != 0)
#endif /* HAVE_GETRESUID */
                {
                        ruid = getuid ();
                }

        if (ruid == 0) {
                return TRUE;
        }

#endif
        return FALSE;
}

/* Call g_free on string when done using it. [transfer: full] */
static gchar *
get_rundir (guint uid)
{
        const gchar *base;

        TRACE ();

        base = RUNDIR "/user";

        return g_strdup_printf ("%s/%d", base, uid);
}

static gboolean
create_rundir_base (guint uid)
{
        const gchar *base;

        TRACE ();

        base = RUNDIR "/user";

        /* Create the base directory that we will own. */
        if (g_mkdir_with_parents (base, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0) {
                g_warning ("Failed to create %s, reason was: %s", base, strerror(errno));
                errno = 0;
                return FALSE;
        }

        /* ensure we have ownership */
        if (chown (base, 0, 0) != 0) {
                g_warning ("Failed to chown %s, reason was: %s", base, strerror(errno));
                errno = 0;
                return FALSE;
        }

        return TRUE;
}

static gboolean
remove_rundir (guint uid, const gchar *dest)
{
        gchar   *command;
        GError  *error = NULL;
        gboolean res;

        TRACE ();

        g_return_val_if_fail (dest, FALSE);

        if (uid < 1) {
                g_debug ("We didn't create a runtime dir for root, nothing to remove");
                return FALSE;
        }

        command = g_strdup_printf (LIBEXECDIR "/ck-remove-directory --uid=%d --dest=%s", uid, dest);

        res = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);

        if (! res) {
                g_warning ("Unable to remove user runtime dir '%s' error was: %s", dest, error->message);
                g_clear_error (&error);
                return FALSE;
        }

        return TRUE;
}

gchar *
ck_generate_runtime_dir_for_user (guint uid)
{
        gchar        *dest;
        struct passwd *pwent;

        TRACE ();

        errno = 0;
        pwent = getpwuid (uid);
        if (pwent == NULL) {
                g_warning ("Unable to lookup UID: %s", g_strerror (errno));
                errno = 0;
                return NULL;
        }

        /* ensure we have created the base directory */
        if (create_rundir_base (uid) == FALSE) {
                return NULL;
        }

        dest = get_rundir (uid);

        /* Ensure any files from the last session are removed */
        if (g_file_test (dest, G_FILE_TEST_EXISTS) == TRUE) {
                remove_rundir (uid, dest);
        }

        /* Create the new directory */
        if (g_mkdir_with_parents (dest, S_IRWXU) != 0) {
                g_warning ("Failed to create XDG_RUNTIME_DIR, reason was: %s", strerror(errno));
                errno = 0;
                g_free (dest);
                return NULL;
        }

        g_debug ("setting uid %d, gid %d", uid, pwent->pw_gid);

        /* assign ownership to the user */
        if (chown (dest, uid, pwent->pw_gid) != 0) {
                g_warning ("Failed to chown XDG_RUNTIME_DIR, reason was: %s", strerror(errno));
                errno = 0;
                g_free (dest);
                return NULL;
        }

        /* attempt to make it a small tmpfs location */
        ck_make_tmpfs (uid, pwent->pw_gid, dest);

        return dest;
}

gboolean
ck_remove_runtime_dir_for_user (guint uid)
{
        gchar        *dest;

        TRACE ();

        dest = get_rundir (uid);

        /* attempt to remove the tmpfs */
        ck_remove_tmpfs (uid, dest);

        /* remove the user's runtime dir now that all user sessions
         * are gone */
        remove_rundir (uid, dest);

        g_free (dest);

        return TRUE;
}

gboolean
ck_wait_for_active_console_num (int   console_fd,
                                guint num)
{
        gboolean ret;
        int      res;

        g_assert (console_fd != -1);

 again:
        ret = FALSE;

        errno = 0;
#ifdef VT_WAITACTIVE
        g_debug ("VT_WAITACTIVE for vt %d", num);
        res = ioctl (console_fd, VT_WAITACTIVE, num);
        g_debug ("VT_WAITACTIVE for vt %d returned %d", num, res);
#else
        res = ERROR;
        errno = ENOTSUP;
#endif

        if (res == ERROR) {
                const char *errmsg;

                errmsg = g_strerror (errno);

                if (errno == EINTR) {
                        g_debug ("Interrupted waiting for native console %d activation: %s",
                                  num,
                                  errmsg);
                       goto again;
                } else if (errno == ENOTSUP) {
                        g_debug ("Console activation not supported on this system");
                } else {
                        g_warning ("Error waiting for native console %d activation: %s",
                                   num,
                                   errmsg);
                }
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

gboolean
ck_activate_console_num (int   console_fd,
                         guint num)
{
        gboolean ret;
        int      res;

        g_assert (console_fd != -1);

        ret = FALSE;

        errno = 0;
#ifdef VT_ACTIVATE
        res = ioctl (console_fd, VT_ACTIVATE, num);
#else
        res = ERROR;
        errno = ENOTSUP;
#endif

        if (res == 0) {
                ret = TRUE;
        } else {
                if (errno == ENOTSUP) {
                        g_debug ("Console activation not supported on this system");
                } else {
                        g_warning ("Unable to activate console: %s", g_strerror (errno));
                }
        }

        return ret;
}
