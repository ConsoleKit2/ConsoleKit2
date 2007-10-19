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
        REPORT_TYPE_LAST_COMPAT,
        REPORT_TYPE_LOG,
} ReportType;

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/run/ConsoleKit/history"
#define MAX_LINE_LEN 2048

static GList *all_events = NULL;

static gboolean
process_event_line (const char *line)
{
        GString    *str;
        CkLogEvent *event;

        str = g_string_new (line);
        event = ck_log_event_new_from_string (str);
        g_string_free (str, TRUE);

        if (event != NULL) {
                all_events = g_list_prepend (all_events, event);
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
generate_report_summary (void)
{
}

static CkLogEvent *
find_first_matching_remove_event (GList                      *events,
                                  CkLogSeatSessionAddedEvent *event)
{
        CkLogEvent *revent;
        GList      *l;

        revent = NULL;

        for (l = events; l != NULL; l = l->next) {
                CkLogSeatSessionRemovedEvent *e;

                if (((CkLogEvent *)l->data)->type != CK_LOG_EVENT_SEAT_SESSION_REMOVED) {
                        continue;
                }
                e = l->data;

                if (e->session_id != NULL
                    && event->session_id != NULL
                    && strcmp (e->session_id, event->session_id) == 0) {
                        revent = (CkLogEvent *)l->data;
                        break;
                }
        }

        return revent;
}

static char *
get_user_name_for_uid (uid_t uid)
{
        struct passwd *pwent;
        char          *name;

        name = NULL;

        pwent = getpwuid (uid);

        if (pwent != NULL) {
                name = g_strdup (pwent->pw_name);
        }

        return name;
}

static char *
get_utline_for_event (CkLogSeatSessionAddedEvent *e)
{
        char *utline;

        if (e->session_x11_display != NULL && e->session_x11_display[0] != '\0') {
                utline = g_strdup (e->session_x11_display);
        } else {
                if (g_str_has_prefix (e->session_display_device, "/dev/")) {
                        utline = g_strdup (e->session_display_device + 5);
                }
        }

        return utline;
}

static void
generate_report_last_compat (void)
{
        GList      *oldest;
        CkLogEvent *oldest_event;
        GList      *l;

        /* print events in reverse time order */

        for (l = g_list_last (all_events); l != NULL; l = l->prev) {
                CkLogEvent                 *event;
                GString                    *str;
                char                       *username;
                char                       *utline;
                char                       *addedtime;
                char                       *removedtime;
                char                       *duration;
                CkLogSeatSessionAddedEvent *e;
                CkLogEvent                 *remove_event;

                event = l->data;

                if (event->type != CK_LOG_EVENT_SEAT_SESSION_ADDED) {
                        continue;
                }

                e = (CkLogSeatSessionAddedEvent *)event;

                str = g_string_new (NULL);

                username = get_user_name_for_uid (e->session_unix_user);
                utline = get_utline_for_event (e);

                addedtime = g_strndup (ctime (&event->timestamp.tv_sec), 16);
                g_string_printf (str,
                                 "%-8.8s %-12.12s %-16.16s %-16.16s",
                                 username,
                                 utline,
                                 e->session_remote_host_name ? e->session_remote_host_name : "",
                                 addedtime);
                g_free (username);
                g_free (addedtime);
                g_free (utline);

                remove_event = find_first_matching_remove_event (l, e);
                if (remove_event != NULL) {
                        time_t secs;
                        int    mins;
                        int    hours;
                        int    days;

                        removedtime = g_strdup_printf ("- %s", ctime (&remove_event->timestamp.tv_sec) + 11);
                        removedtime[7] = 0;
                        secs = remove_event->timestamp.tv_sec - event->timestamp.tv_sec;
                        mins  = (secs / 60) % 60;
                        hours = (secs / 3600) % 24;
                        days  = secs / 86400;
                        if (days > 0) {
                                duration = g_strdup_printf ("(%d+%02d:%02d)", days, hours, mins);
                        } else {
                                duration = g_strdup_printf (" (%02d:%02d)", hours, mins);
                        }
                } else {
                        removedtime = g_strdup ("  still");
                        duration = g_strdup ("logged in");
                }

                g_string_append_printf (str,
                                        " %-7.7s %-12.12s",
                                        removedtime,
                                        duration);

                g_print ("%s\n", str->str);
                g_string_free (str, TRUE);
                g_free (removedtime);
                g_free (duration);
        }

        oldest = g_list_first (all_events);
        if (oldest != NULL) {
                oldest_event = oldest->data;
                g_print ("\nLog begins %s", ctime (&oldest_event->timestamp.tv_sec));
        }
}

static void
generate_report_log (void)
{
        GList *l;

        for (l = all_events; l != NULL; l = l->next) {
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
generate_report (int report_type)
{

        all_events = g_list_reverse (all_events);

        switch (report_type) {
        case REPORT_TYPE_SUMMARY:
                generate_report_summary ();
                break;
        case REPORT_TYPE_LAST_COMPAT:
                generate_report_last_compat ();
                break;
        case REPORT_TYPE_LOG:
                generate_report_log ();
                break;
        default:
                g_assert_not_reached ();
                break;
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
        int                 report_type;
        static gboolean     do_version = FALSE;
        static gboolean     report_last_compat = FALSE;
        static gboolean     report_log = FALSE;
        static GOptionEntry entries [] = {
                { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version, N_("Version of this application"), NULL },
                { "last-compat", 'l', 0, G_OPTION_ARG_NONE, &report_last_compat, N_("Show 'last' compatible listing of last logged in users"), NULL },
                { "log", 'a', 0, G_OPTION_ARG_NONE, &report_log, N_("Show full event log"), NULL },
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

        if (report_last_compat) {
                report_type = REPORT_TYPE_LAST_COMPAT;
        } else if (report_log) {
                report_type = REPORT_TYPE_LOG;
        } else {
                report_type = REPORT_TYPE_SUMMARY;
        }

        process_log_file (DEFAULT_LOG_FILENAME);
        generate_report (report_type);
        free_events ();

        return 0;
}
