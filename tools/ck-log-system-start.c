/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __FreeBSD__
#include <kenv.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <string.h>

#include <libintl.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "ck-log-event.h"

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/log/ConsoleKit/history"

#ifdef __linux__
#define LINUX_KERNEL_CMDLINE "/proc/cmdline"
#endif

/* Adapted from auditd auditd-event.c */
static gboolean
open_log_file (const char *filename,
               int        *fdp,
               FILE      **filep)
{
        int      flags;
        int      fd;
        int      res;
        char    *dirname;
        FILE    *file;
        gboolean ret;

        ret = FALSE;

        /*
         * Likely errors on rotate: ENFILE, ENOMEM, ENOSPC
         */
        flags = O_WRONLY | O_APPEND;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif

        dirname = g_path_get_dirname (filename);
        /* always make sure we have a directory */
        errno = 0;
        res = g_mkdir_with_parents (dirname,
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning (_("Unable to create directory %s (%s)"),
                           dirname,
                           g_strerror (errno));
                g_free (dirname);
                return FALSE;
        }
        g_free (dirname);

retry:
        fd = g_open (filename, flags, 0600);
        if (fd < 0) {
                if (errno == ENOENT) {
                        /* FIXME: should we just skip if file doesn't exist? */
                        fd = g_open (filename,
                                     O_CREAT | O_EXCL | O_APPEND,
                                     S_IRUSR | S_IWUSR | S_IRGRP);
                        if (fd < 0) {
                                g_warning (_("Couldn't create log file %s (%s)"),
                                           filename,
                                           g_strerror (errno));
                                goto out;
                        }

                        close (fd);
                        fd = g_open (filename, flags, 0600);
                } else if (errno == ENFILE) {
                        /* All system descriptors used, try again... */
                        goto retry;
                }
                if (fd < 0) {
                        g_warning (_("Couldn't open log file %s (%s)"),
                                   filename,
                                   g_strerror (errno));
                        goto out;
                }
        }

        if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
                close (fd);
                g_warning (_("Error setting log file CLOEXEC flag (%s)"),
                           g_strerror (errno));
                goto out;
        }

        if (fchown (fd, 0, 0) == -1) {
                g_warning (_("Error changing owner of log file (%s)"),
                           g_strerror (errno));
        }

        file = fdopen (fd, "a");
        if (file == NULL) {
                g_warning (_("Error setting up log descriptor (%s)"),
                           g_strerror (errno));
                close (fd);
                goto out;
        }

        /* Set it to line buffering */
        setlinebuf (file);

        ret = TRUE;

        if (fdp != NULL) {
                *fdp = fd;
        }
        if (filep != NULL) {
                *filep = file;
        }

 out:

        return ret;
}

static gboolean
write_log_for_event (CkLogEvent *event)
{
        GString *str;
        FILE    *file;
        int      fd;

        str = g_string_new (NULL);

        ck_log_event_to_string (event, str);

        if (! open_log_file (DEFAULT_LOG_FILENAME, &fd, &file)) {
                exit (1);
        }

        if (file != NULL) {
                int rc;

                rc = fprintf (file, "%s\n", str->str);
                if (rc <= 0) {
                        g_warning (_("Record was not written to disk (%s)"),
                                   g_strerror (errno));
                }
        } else {
                g_warning (_("Log file not open for writing"));
        }

        g_string_free (str, TRUE);

        return TRUE;
}

static char *
get_boot_arguments (void)
{
#if defined(__linux__)
        char *contents;
        gboolean res;

        contents = NULL;
        res = g_file_get_contents (LINUX_KERNEL_CMDLINE,
                                   &contents,
                                   NULL,
                                   NULL);
        if (!res) {
                g_free (contents);
                contents = NULL;
        } else {
                g_strchomp (contents);
        }

        return contents;
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        char kern_name[1024], root[1024], mopts[1024];
        char *options;

        options = NULL;
        if (kenv(KENV_GET, "kernelname", kern_name, sizeof (kern_name)) == -1) {
                return options;
        }

        if (kenv(KENV_GET, "vfs.root.mountfrom.options", mopts, sizeof (mopts)) == -1) {
                g_strlcpy (mopts, "ro", sizeof (mopts));
        }

        if (kenv(KENV_GET, "vfs.root.mountfrom", root, sizeof (root)) == -1) {
                g_strlcpy (root, "/", sizeof (root));
        }

        options = g_strdup_printf ("%s %s root=%s", mopts, kern_name, root);

        return options;
#else
        return NULL;
#endif
}

int
main (int    argc,
      char **argv)
{
        CkLogEvent event;
        CkLogSystemStartEvent *e;
        struct utsname uts;

        /* Setup for i18n */
        setlocale(LC_ALL, "");
 
#ifdef ENABLE_NLS
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);
#endif

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SYSTEM_START;
        g_get_current_time (&event.timestamp);
        e = (CkLogSystemStartEvent *) &event;

        if (uname (&uts) == 0) {
                e->kernel_release = uts.release;
        }

        e->boot_arguments = get_boot_arguments ();

        write_log_for_event (&event);

        return 0;
}
