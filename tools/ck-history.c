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
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "ck-log-event.h"

typedef enum {
        REPORT_TYPE_SUMMARY = 0,
} ReportType;

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/run/ConsoleKit/history"
#define MAX_LINE_LEN 2048

static GList *events = NULL;

static gboolean
process_event_line (const char *line)
{
        GString    *str;
        CkLogEvent *event;

        str = g_string_new (line);
        event = ck_log_event_new_from_string (str);
        g_string_free (str, TRUE);

        if (event != NULL) {
                events = g_list_prepend (events, event);
        }

        return TRUE;
}

static gboolean
process_log_stream (FILE *fstream)
{
        char line[MAX_LINE_LEN];

        while (fgets (line, sizeof (line), fstream) != NULL) {
                if (strlen (line) == sizeof (line) - 1) {
                        g_warning ("Log line truncated");
                }

                process_event_line (line);
        }

        return TRUE;
}

static gboolean
process_log_file (const char *filename)
{
        FILE *f;

        f = g_fopen (filename, "r");
        if (f == NULL) {
                g_warning ("Error opening %s (%s)\n",
                           filename,
                           g_strerror (errno));
                return FALSE;
        }

        return process_log_stream (f);
}

static void
generate_report (void)
{
        GList *l;

        events = g_list_reverse (events);

        for (l = events; l != NULL; l = l->next) {
                CkLogEvent *event;
                GString    *str;

                event = l->data;
                str = g_string_new (NULL);
                ck_log_event_to_string (event, str);
                g_print ("%s\n", str->str);
                g_string_free (str, TRUE);
        }
}

static void
free_events (void)
{
        /* FIXME: */
}

int
main (int    argc,
      char **argv)
{
        GOptionContext     *context;
        gboolean            retval;
        GError             *error = NULL;
        static gboolean     do_version = FALSE;
        static GOptionEntry entries [] = {
                { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version, N_("Version of this application"), NULL },
                { NULL }
        };

        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, NULL);
        retval = g_option_context_parse (context, &argc, &argv, &error);

        g_option_context_free (context);

        if (! retval) {
                g_warning ("%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (do_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        process_log_file (DEFAULT_LOG_FILENAME);
        generate_report ();
        free_events ();

        return 0;
}
