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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <glib.h>

#include "ck-sysdeps.h"

static char *
get_tty_for_pid (int pid)
{
        GError        *error;
        char          *device;
        gboolean       res;
        CkProcessStat *xorg_stat;

        error = NULL;
        res = ck_process_stat_new_for_unix_pid (pid, &xorg_stat, &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("stat on pid %d failed: %s", pid, error->message);
                        g_error_free (error);
                }
                /* keep the tty value */
                return NULL;
        }

        device = ck_process_stat_get_tty (xorg_stat);
        ck_process_stat_free (xorg_stat);
        return device;
}

static Display *
display_init (const char *display_name)
{
        Display    *xdisplay;

        if (display_name == NULL) {
                display_name = g_getenv ("DISPLAY");
        }

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

static char *
get_tty_for_display (Display *xdisplay)
{
        Window root_window;
        Atom xfree86_vt_atom;
        Atom return_type_atom;
        int return_format;
        gulong return_count;
        gulong bytes_left;
        guchar *return_value;
        glong vt;
        char *display;

        display = NULL;

        xfree86_vt_atom = XInternAtom (xdisplay, "XFree86_VT", True);

        if (xfree86_vt_atom == None) {
                return NULL;
        }

        root_window = DefaultRootWindow (xdisplay);

        g_assert (root_window != None);

        return_value = NULL;
        if (XGetWindowProperty(xdisplay, root_window, xfree86_vt_atom,
                               0L, 1L, False, XA_INTEGER,
                               &return_type_atom, &return_format,
                               &return_count, &bytes_left,
                               &return_value) != Success) {
                goto out;
        }

        if (return_type_atom != XA_INTEGER) {
                goto out;
        }

        if (return_format != 32) {
                goto out;
        }

        if (return_count != 1) {
                goto out;
        }

        if (bytes_left != 0) {
                goto out;
        }

        vt = *((glong *) return_value);

        if (vt <= 0) {
                goto out;
        }

        display = g_strdup_printf ("/dev/tty%ld", vt);

out:
        if (return_value != NULL) {
                XFree (return_value);
        }

        return display;
}

int
main (int    argc,
      char **argv)
{
        int      fd;
        int      ret;
        Display *xdisplay;
        static char *display = NULL;
        GError             *error;
        GOptionContext     *context;
        static GOptionEntry entries [] = {
                { "display", 0, 0, G_OPTION_ARG_STRING, &display, "display name", NULL },
                { NULL }
        };

        ret = 1;

        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, NULL);
        error = NULL;
        ret = g_option_context_parse (context, &argc, &argv, &error);
        g_option_context_free (context);

        xdisplay = display_init (display);

        fd = ConnectionNumber (xdisplay);

        if (fd > 0) {
                int      pid;
                char    *device;
                gboolean res;

                ret = 0;
                res = ck_get_socket_peer_credentials (fd, &pid, NULL, NULL);
                if (res) {
                        if (pid > 0) {
                                device = get_tty_for_pid (pid);

                                if (device == NULL) {
                                        device = get_tty_for_display (xdisplay);
                                }

                                if (device != NULL) {
                                        printf ("%s\n", device);
                                        g_free (device);
                                }
                        }
                }
        }

	return ret;
}
