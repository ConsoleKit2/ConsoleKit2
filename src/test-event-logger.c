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

#include "ck-event-logger.h"

static gboolean
write_to_log (CkEventLogger    *logger)
{
        CkLogEvent event;
        GError    *error;
        gboolean   res;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_SESSION_ADDED;
        g_get_current_time (&event.timestamp);

        event.event.seat_session_added.session_id = "Session1";

        error = NULL;
        res = ck_event_logger_queue_event (logger, &event, &error);
        if (! res) {
                g_warning ("Unable to queue event: %s", error->message);
                g_error_free (error);
        }

        return TRUE;
}

int
main (int argc, char **argv)
{
        GMainLoop        *loop;
        CkEventLogger    *logger;
        char             *filename;

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }
        g_type_init ();

        filename = g_build_filename (g_get_tmp_dir (), "ck-logger-test.log", NULL);

        g_message ("Testing the event logger.\n  Should write messages to: %s", filename);
        logger = ck_event_logger_new (filename);
        g_free (filename);

        g_timeout_add (1000, (GSourceFunc)write_to_log, logger);

        loop = g_main_loop_new (NULL, FALSE);

        g_main_loop_run (loop);

        g_object_unref (logger);

        g_main_loop_unref (loop);

        return 0;
}
