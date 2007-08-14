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

static void
print_peer_pid (int fd)
{
#ifdef SO_PEERCRED
        struct ucred cr;
        socklen_t cr_len;

        cr_len = sizeof (cr);

        if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len) == 0 && cr_len == sizeof (cr)) {
                /* paranoia check for peer running as root */
                if (cr.uid == 0) {
                        printf ("%u\n", cr.pid);
                }
        } else {
                g_warning ("Failed to getsockopt() credentials, returned len %d/%d: %s\n",
                           cr_len,
                           (int) sizeof (cr),
                           g_strerror (errno));
        }
#endif
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
                ret = 0;
                print_peer_pid (fd);
        }

	return ret;
}
