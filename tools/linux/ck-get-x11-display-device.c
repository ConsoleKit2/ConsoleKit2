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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <X11/Xlib.h>
#include <glib.h>

#include "proc.h"

static char *
get_tty_for_pid (int pid)
{
        GError *error;
        char   *device;
        gboolean res;
        proc_stat_t *xorg_stat;

        error = NULL;
        res = proc_stat_new_for_pid (pid, &xorg_stat, &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("stat on pid %d failed: %s", pid, error->message);
                        g_error_free (error);
                }
                /* keep the tty value */
                return NULL;
        }

        device = proc_stat_get_tty (xorg_stat);
        proc_stat_free (xorg_stat);
        return device;
}

static int
get_peer_pid (int fd)
{
        int pid = -1;
#ifdef SO_PEERCRED
        struct ucred cr;
        socklen_t cr_len;

        cr_len = sizeof (cr);

        if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len) == 0 && cr_len == sizeof (cr)) {
                /* paranoia check for peer running as root */
                if (cr.uid == 0) {
                        pid = cr.pid;
                }
        } else {
                g_warning ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
                           cr_len,
                           (int) sizeof (cr),
                           g_strerror (errno));
        }
#endif
        return pid;
}

static Display *
display_init (int *argc, char ***argv)
{
        const char *display_name;
        Display    *xdisplay;

        display_name = g_getenv ("DISPLAY");

        if (display_name == NULL) {
                g_warning ("DISPLAY is not set");
                exit (1);
        }

        xdisplay = XOpenDisplay (display_name);
        if (xdisplay == NULL) {
                g_warning ("cannot open display: %s", display_name ? display_name : "");
                exit (1);
        }

        return xdisplay;
}

int
main (int    argc,
      char **argv)
{
        int      fd;
        int      ret;
        Display *xdisplay;

        ret = 1;

        xdisplay = display_init (&argc, &argv);

        fd = ConnectionNumber (xdisplay);

        if (fd > 0) {
                int pid;
                char *device;

                ret = 0;
                pid = get_peer_pid (fd);
                if (pid != -1) {
                        device = get_tty_for_pid (pid);
                        if (device != NULL) {
                                printf ("%s\n", device);
                                g_free (device);
                        }
                }
        }

	return ret;
}
