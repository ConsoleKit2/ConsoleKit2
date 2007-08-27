/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

#include <glib.h>

#include "ck-tty-idle-monitor.h"
#include "ck-sysdeps.h"

static void
idle_changed_cb (CkTtyIdleMonitor *monitor,
                 gboolean          idle_hint,
                 gpointer          data)
{
        g_message ("idle hint changed: %s", idle_hint ? "idle" : "not idle");
}

static gboolean
is_console (const char *device)
{
        int      fd;
        gboolean ret;

        ret = FALSE;
        fd = open (device, O_RDONLY | O_NOCTTY);
        if (fd < 0) {
                goto out;
        }

        ret = ck_fd_is_a_console (fd);

        close (fd);

 out:
        return ret;
}

int
main (int argc, char **argv)
{
        GMainLoop        *loop;
        CkTtyIdleMonitor *monitor;
        char             *device;

        g_type_init ();

        if (argc < 2) {
                device = ttyname (0);
        } else {
                device = g_strdup (argv[1]);
        }

        if (! is_console (device)) {
                g_warning ("Device is not a console");
                exit (1);
        }

        g_message ("Testing the TTY idle monitor.\n1. Wait for idleness to be detected.\n2. Hit keys on the keyboard to see if activity is noticed.");

        monitor = ck_tty_idle_monitor_new (device);

        g_signal_connect (monitor,
                          "idle-hint-changed",
                          G_CALLBACK (idle_changed_cb),
                          NULL);
        ck_tty_idle_monitor_set_threshold (monitor, 5);
        ck_tty_idle_monitor_start (monitor);

        loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (loop);

        g_object_unref (monitor);

        g_main_loop_unref (loop);

        return 0;
}
