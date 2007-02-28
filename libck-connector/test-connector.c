/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 David Zeuthen <davidz@redhat.com>
 * Copyright (c) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ck-connector.h"

int
main (int argc, char *argv[])
{
        CkConnector *connector;
        int          ret;
        DBusError    error;

        ret = 1;

        connector = ck_connector_new ();
        if (connector == NULL) {
                printf ("OOM creating CkConnector\n");
                goto out;
        }

        dbus_error_init (&error);
        if (! ck_connector_open_session_for_user (connector, 500, "/dev/tty2", ":1", &error)) {
                if (dbus_error_is_set (&error)) {
                        printf ("%s\n",
                                error.message);
                        dbus_error_free (&error);
                } else {
                        printf ("cannot open CK session: OOM, D-Bus system bus not available,\n"
                                "ConsoleKit not available or insufficient privileges.\n");
                }
                goto out;
        }

        printf ("Session cookie is '%s'\n", ck_connector_get_cookie (connector));
        sleep (20);

        dbus_error_init (&error);
        if (! ck_connector_close_session (connector, &error)) {
                if (dbus_error_is_set (&error)) {
                        printf ("%s\n",
                                error.message);
                        dbus_error_free (&error);
                } else {
                        printf ("Cannot close CK session: OOM, D-Bus system bus not available,\n"
                                "ConsoleKit not available or insufficient privileges.\n");
                }
                goto out;
        }

        ret = 0;
out:
        if (connector != NULL) {
                ck_connector_unref (connector);
        }

        return ret;
}
