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

#include "ck-sysdeps.h"

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
                int      pid;
                gboolean res;

                ret = 0;
                res = ck_get_socket_peer_credentials (fd, &pid, NULL, NULL);
                if (res) {
                        if (pid > 0) {
                                printf ("%d\n", pid);
                        }
                }
        }

	return ret;
}
