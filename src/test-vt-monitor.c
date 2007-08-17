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

#include "ck-vt-monitor.h"
#include "ck-sysdeps.h"

static void
activated_cb (CkVtMonitor *monitor,
              guint        num,
              gpointer     data)
{
        g_message ("VT %u activated", num);
}

int
main (int argc, char **argv)
{
        GMainLoop        *loop;
        CkVtMonitor      *monitor;
        GError           *error;
        guint             num;
        gboolean          res;

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }
        g_type_init ();

        if (! ck_is_root_user ()) {
                g_warning ("Must be run as root");
                exit (1);
        }

        g_message ("Testing the VT monitor.\n  Should print messages when VT is switched.");

        monitor = ck_vt_monitor_new ();

        res = ck_vt_monitor_get_active (monitor, &num, &error);
        if (! res) {
                g_warning ("Couldn't determine active VT: %s", error->message);
                exit (1);
        }

        g_message ("VT %u is currently active", num);

        g_signal_connect (monitor,
                          "active-changed",
                          G_CALLBACK (activated_cb),
                          NULL);

        loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (loop);

        g_object_unref (monitor);

        g_main_loop_unref (loop);

        return 0;
}
