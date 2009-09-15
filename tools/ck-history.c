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
#include <pwd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>
#include <zlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "ck-log-event.h"

typedef enum {
        REPORT_TYPE_SUMMARY = 0,
        REPORT_TYPE_LAST,
        REPORT_TYPE_LAST_COMPAT,
        REPORT_TYPE_FREQUENT,
        REPORT_TYPE_LOG,
} ReportType;

/* same record types as sysvinit last */
typedef enum {
        RECORD_STATUS_CRASH = 1,  /* No logout record, system boot in between */
        RECORD_STATUS_DOWN,       /* System brought down in decent way */
        RECORD_STATUS_NORMAL,     /* Normal */
        RECORD_STATUS_NOW,        /* Still logged in */
        RECORD_STATUS_REBOOT,     /* Reboot record. */
        RECORD_STATUS_PHANTOM,    /* No logout record but session is stale. */
        RECORD_STATUS_TIMECHANGE, /* NEW_TIME or OLD_TIME */
} RecordStatus;

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/log/ConsoleKit/history"
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
process_log_gzstream (gzFile *fstream)
{
        char line[MAX_LINE_LEN];

        while (gzgets (fstream, line, sizeof (line)) != Z_NULL) {
                if (strlen (line) == sizeof (line) - 1) {
                        g_warning ("Log line truncated");
                }

                process_event_line (line);
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
        gboolean ret;

        if (g_str_has_suffix (filename, ".gz")) {
                gzFile *f;
                f = gzopen (filename, "r");
                if (f == NULL) {
                        int         errnum;
                        const char *errmsg;
                        errmsg = gzerror (f, &errnum);
                        if (errnum == Z_ERRNO) {
                                errmsg = g_strerror (errno);
                        }
                        g_warning ("Error opening %s (%s)\n",
                                   filename,
                                   errmsg);
                        return FALSE;
                }
                ret = process_log_gzstream (f);
                gzclose (f);
        } else {
                FILE    *f;

                f = g_fopen (filename, "r");
                if (f == NULL) {
                        g_warning ("Error opening %s (%s)\n",
                                   filename,
                                   g_strerror (errno));
                        return FALSE;
                }
                ret = process_log_stream (f);
                fclose (f);
        }

        return ret;
}

static GList *
get_log_file_list (void)
{
        int      num;
        GList   *files;

        /* always try the primary file */
        files = NULL;
        files = g_list_prepend (files, g_strdup (DEFAULT_LOG_FILENAME));
        num = 1;
        while (1) {
                char *filename;
                filename = g_strdup_printf ("%s.%d", DEFAULT_LOG_FILENAME, num);
                if (g_access (filename, R_OK) != 0) {
                        char *filename_gz;

                        /* check for .gz */
                        filename_gz = g_strdup_printf ("%s.gz", filename);
                        g_free (filename);

                        if (g_access (filename_gz, R_OK) != 0) {
                                g_free (filename_gz);
                                break;
                        }
                        filename = filename_gz;
                }
                num++;
                files = g_list_prepend (files, filename);
        };

        return files;
}

static gboolean
process_logs (void)
{
        gboolean ret;
        GList   *files;
        GList   *l;

        ret = FALSE;

        files = get_log_file_list ();

        for (l = files; l != NULL; l = l->next) {
                gboolean res;
                char    *filename;

                filename = l->data;

                res = process_log_file (filename);
                if (! res) {
                        goto out;
                }
        }

        ret = TRUE;

 out:
        g_list_foreach (files, (GFunc)g_free, NULL);
        g_list_free (files);

        return ret;
}

static void
generate_report_summary (int         uid,
                         const char *seat,
                         const char *session_type)
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
                CkLogEventType etype;

                etype = ((CkLogEvent *)l->data)->type;
                /* skip all non removal events */
                if (! (etype == CK_LOG_EVENT_SEAT_SESSION_REMOVED
                       || etype == CK_LOG_EVENT_SYSTEM_START
                       || etype == CK_LOG_EVENT_SYSTEM_STOP
                       || etype == CK_LOG_EVENT_SYSTEM_RESTART)) {
                        continue;
                }

                if (etype == CK_LOG_EVENT_SEAT_SESSION_REMOVED) {
                        CkLogSeatSessionRemovedEvent *e;
                        e = l->data;

                        if (e->session_id != NULL
                            && event->session_id != NULL
                            && strcmp (e->session_id, event->session_id) == 0) {
                                revent = (CkLogEvent *)l->data;
                                break;
                        }
                } else {
                        revent = (CkLogEvent *)l->data;
                        break;
                }
        }

        return revent;
}

static CkLogEvent *
find_first_matching_system_stop_event (GList                      *events,
                                       CkLogSeatSessionAddedEvent *event)
{
        CkLogEvent *revent;
        GList      *l;

        revent = NULL;

        for (l = events; l != NULL; l = l->next) {
                CkLogEventType etype;

                etype = ((CkLogEvent *)l->data)->type;

                /* skip all non removal events */
                if (! (etype == CK_LOG_EVENT_SYSTEM_STOP
                       || etype == CK_LOG_EVENT_SYSTEM_RESTART)) {
                        continue;
                }

                revent = (CkLogEvent *)l->data;
                break;
        }

        return revent;
}

static char *
get_user_name_for_uid (int  uid)
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

static int
get_uid_for_username (const char *username)
{
        struct passwd *pwent;
        int            uid;

        g_assert (username != NULL);

        uid = -1;

        pwent = getpwnam (username);
        if (pwent != NULL) {
                uid = pwent->pw_uid;
        }

        return uid;
}

static char *
get_utline_for_event (CkLogEvent *event)
{
        char *utline;

        utline = NULL;

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                {
                        CkLogSeatSessionAddedEvent *e;
                        e = (CkLogSeatSessionAddedEvent *)event;
                        if (e->session_x11_display_device != NULL && e->session_x11_display_device[0] != '\0') {
                                if (g_str_has_prefix (e->session_x11_display_device, "/dev/")) {
                                        utline = g_strdup (e->session_x11_display_device + 5);
                                } else {
                                        utline = g_strdup (e->session_x11_display_device);
                                }
                        } else {
                                if (g_str_has_prefix (e->session_display_device, "/dev/")) {
                                        utline = g_strdup (e->session_display_device + 5);
                                } else {
                                        utline = g_strdup (e->session_display_device);
                                }
                        }
                }
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                utline = g_strdup ("system boot");
                break;
        default:
                g_assert_not_reached ();
        }

        return utline;
}

static char *
get_user_name_for_event (CkLogEvent *event)
{
        char *username;

        username = NULL;

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                username = get_user_name_for_uid (((CkLogSeatSessionAddedEvent *)event)->session_unix_user);
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                username = g_strdup ("reboot");
                break;
        default:
                g_assert_not_reached ();
        }

        return username;
}

static char *
get_host_for_event (CkLogEvent *event)
{
        char *name;

        name = NULL;

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                name = g_strdup (((CkLogSeatSessionAddedEvent *)event)->session_remote_host_name);
                if (name == NULL) {
                        /* If not set then use the display value */
                        name = g_strdup (((CkLogSeatSessionAddedEvent *)event)->session_x11_display);
                }
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                name = g_strdup (((CkLogSystemStartEvent *)event)->kernel_release);
                break;
        default:
                g_assert_not_reached ();
        }

        return name;
}

static RecordStatus
get_event_record_status (CkLogEvent *remove_event)
{
        RecordStatus status;

        status = RECORD_STATUS_NOW;

        if (remove_event == NULL) {
                goto out;
        }

        if (remove_event->type == CK_LOG_EVENT_SEAT_SESSION_REMOVED) {
                status = RECORD_STATUS_NORMAL;
        } else if (remove_event->type == CK_LOG_EVENT_SYSTEM_START) {
                status = RECORD_STATUS_CRASH;
        } else if (remove_event->type == CK_LOG_EVENT_SYSTEM_STOP) {
                status = RECORD_STATUS_DOWN;
        } else if (remove_event->type == CK_LOG_EVENT_SYSTEM_RESTART) {
                status = RECORD_STATUS_DOWN;
        }

 out:
        return status;
}

static char *
get_duration (CkLogEvent *event,
              CkLogEvent *remove_event)
{
        time_t secs;
        int    mins;
        int    hours;
        int    days;
        char  *duration;

        if (remove_event != NULL) {
                secs = remove_event->timestamp.tv_sec - event->timestamp.tv_sec;
        } else {
                GTimeVal now;
                g_get_current_time (&now);
                secs = now.tv_sec - event->timestamp.tv_sec;
        }

        mins  = (secs / 60) % 60;
        hours = (secs / 3600) % 24;
        days  = secs / 86400;
        if (days > 0) {
                duration = g_strdup_printf ("(%d+%02d:%02d)", days, hours, mins);
        } else {
                duration = g_strdup_printf (" (%02d:%02d)", hours, mins);
        }
        return duration;
}

static void
print_last_report_record (GList      *list,
                          CkLogEvent *event,
                          gboolean    legacy_compat)
{
        GString                    *str;
        char                       *username;
        char                       *utline;
        char                       *host;
        char                       *addedtime;
        char                       *removedtime;
        char                       *duration;
        char                       *session_type;
        char                       *session_id;
        char                       *seat_id;
        CkLogSeatSessionAddedEvent *e;
        CkLogEvent                 *remove_event;
        RecordStatus                status;

        if (event->type != CK_LOG_EVENT_SEAT_SESSION_ADDED
            && event->type != CK_LOG_EVENT_SYSTEM_START) {
                return;
        }

        remove_event = NULL;

        if (event->type == CK_LOG_EVENT_SEAT_SESSION_ADDED) {
                e = (CkLogSeatSessionAddedEvent *)event;

                remove_event = find_first_matching_remove_event (list->next, e);
                status = get_event_record_status (remove_event);

                session_type = e->session_type;
                session_id = e->session_id;
                seat_id = e->seat_id;
        } else {
                status = RECORD_STATUS_REBOOT;
                remove_event = find_first_matching_system_stop_event (list->next, e);

                session_type = "";
                session_id = "";
                seat_id = "";
        }

        str = g_string_new (NULL);

        username = get_user_name_for_event (event);
        utline = get_utline_for_event (event);
        host = get_host_for_event (event);

        addedtime = g_strndup (ctime (&event->timestamp.tv_sec), 16);

        if (legacy_compat) {
                g_string_printf (str,
                                 "%-8.8s %-12.12s %-16.16s %-16.16s",
                                 username,
                                 utline != NULL ? utline : "",
                                 host != NULL ? host : "",
                                 addedtime);
        } else {
                g_string_printf (str,
                                 "%-8.8s %12s %-10.10s %-7.7s %-12.12s %-28.28s %-16.16s",
                                 username,
                                 session_type,
                                 session_id,
                                 seat_id,
                                 utline,
                                 host != NULL ? host : "",
                                 addedtime);
        }

        g_free (username);
        g_free (addedtime);
        g_free (utline);
        g_free (host);

        removedtime = NULL;
        duration = NULL;

        switch (status) {
        case RECORD_STATUS_CRASH:
                duration = get_duration (event, remove_event);
                removedtime = g_strdup ("- crash");
                break;
        case RECORD_STATUS_DOWN:
                duration = get_duration (event, remove_event);
                removedtime = g_strdup ("- down ");
                break;
        case RECORD_STATUS_NOW:
                duration = g_strdup ("logged in");
                removedtime = g_strdup ("  still");
                break;
        case RECORD_STATUS_PHANTOM:
                duration = g_strdup ("- no logout");
                removedtime = g_strdup ("   gone");
                break;
        case RECORD_STATUS_REBOOT:
                duration = get_duration (event, remove_event);
                removedtime = g_strdup ("");
                break;
        case RECORD_STATUS_TIMECHANGE:
                duration = g_strdup ("");
                removedtime = g_strdup ("");
                break;
        case RECORD_STATUS_NORMAL:
                duration = get_duration (event, remove_event);
                removedtime = g_strdup_printf ("- %s", ctime (&remove_event->timestamp.tv_sec) + 11);
                removedtime[7] = 0;
                break;
        default:
                g_assert_not_reached ();
                break;
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

static void
generate_report_last (int         uid,
                      const char *seat,
                      const char *session_type)
{
        GList      *oldest;
        CkLogEvent *oldest_event;
        GList      *l;

        /* print events in reverse time order */

        for (l = g_list_last (all_events); l != NULL; l = l->prev) {
                CkLogEvent *event;

                event = l->data;

                if (event->type == CK_LOG_EVENT_SEAT_SESSION_ADDED) {
                        CkLogSeatSessionAddedEvent *e;
                        e = (CkLogSeatSessionAddedEvent *)event;

                        if (uid >= 0 && e->session_unix_user != uid) {
                                continue;
                        }

                        if (seat != NULL && e->seat_id != NULL && strcmp (e->seat_id, seat) != 0) {
                                continue;
                        }

                        if (session_type != NULL && e->session_type != NULL && strcmp (e->session_type, session_type) != 0) {
                                continue;
                        }
                }

                print_last_report_record (l, event, FALSE);
        }

        oldest = g_list_first (all_events);
        if (oldest != NULL) {
                oldest_event = oldest->data;
                g_print ("\nLog begins %s", ctime (&oldest_event->timestamp.tv_sec));
        }
}

static void
generate_report_last_compat (int         uid,
                             const char *seat,
                             const char *session_type)
{
        GList      *oldest;
        CkLogEvent *oldest_event;
        GList      *l;

        /* print events in reverse time order */

        for (l = g_list_last (all_events); l != NULL; l = l->prev) {
                CkLogEvent *event;

                event = l->data;

                if (event->type == CK_LOG_EVENT_SEAT_SESSION_ADDED) {
                        CkLogSeatSessionAddedEvent *e;
                        e = (CkLogSeatSessionAddedEvent *)event;

                        if (uid >= 0 && e->session_unix_user != uid) {
                                continue;
                        }

                        if (seat != NULL && e->seat_id != NULL && strcmp (e->seat_id, seat) != 0) {
                                continue;
                        }

                        if (session_type != NULL && e->session_type != NULL && strcmp (e->session_type, session_type) != 0) {
                                continue;
                        }
                }

                print_last_report_record (l, event, TRUE);
        }

        oldest = g_list_first (all_events);
        if (oldest != NULL) {
                oldest_event = oldest->data;
                g_print ("\nLog begins %s", ctime (&oldest_event->timestamp.tv_sec));
        }
}

typedef struct {
        int   uid;
        guint count;
} CountData;

static void
listify_counts (gpointer key,
                gpointer val,
                GList **list)
{
        CountData *count_data;
        count_data = g_new0 (CountData, 1);
        count_data->uid = GPOINTER_TO_INT (key);
        count_data->count = GPOINTER_TO_UINT (val);
        *list = g_list_prepend (*list, count_data);
}

static int
counts_compare (CountData *a,
                CountData *b)
{
        if (a->count < b->count) {
                return 1;
        } else if (a->count > b->count) {
                return -1;
        } else {
                return 0;
        }
}

static void
generate_report_frequent (int         uid,
                          const char *seat,
                          const char *session_type)
{
        GHashTable *counts;
        GList      *l;
        GList      *user_counts;

        /* FIXME: we can probably do this more efficiently */

        counts = g_hash_table_new (NULL, NULL);

        for (l = g_list_first (all_events); l != NULL; l = l->next) {
                CkLogEvent                 *event;
                CkLogSeatSessionAddedEvent *e;
                guint                       count;
                gpointer                    val;

                event = l->data;

                if (event->type != CK_LOG_EVENT_SEAT_SESSION_ADDED) {
                        continue;
                }

                e = (CkLogSeatSessionAddedEvent *)event;

                if (uid >= 0 && e->session_unix_user != uid) {
                        continue;
                }

                if (seat != NULL && e->seat_id != NULL && strcmp (e->seat_id, seat) != 0) {
                        continue;
                }

                if (session_type != NULL && e->session_type != NULL && strcmp (e->session_type, session_type) != 0) {
                        continue;
                }

                val = g_hash_table_lookup (counts, GINT_TO_POINTER (e->session_unix_user));
                if (val != NULL) {
                        count = GPOINTER_TO_INT (val);
                } else {
                        count = 0;
                }

                g_hash_table_insert (counts,
                                     GINT_TO_POINTER (e->session_unix_user),
                                     GUINT_TO_POINTER (count + 1));
        }

        user_counts = NULL;
        g_hash_table_foreach (counts, (GHFunc)listify_counts, &user_counts);
        g_hash_table_destroy (counts);

        if (user_counts == NULL) {
                return;
        }

        user_counts = g_list_sort (user_counts, (GCompareFunc)counts_compare);
        while (user_counts != NULL) {
                CountData *data;
                char      *username;

                data = user_counts->data;

                username = get_user_name_for_uid (data->uid);
                g_print ("%-8.8s %u\n", username, data->count);
                g_free (data);
                user_counts = g_list_delete_link (user_counts, user_counts);
                g_free (username);
        }

        g_list_free (user_counts);
}

static void
generate_report_log (int         uid,
                     const char *seat,
                     const char *session_type)
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
generate_report (int         report_type,
                 int         uid,
                 const char *seat,
                 const char *session_type)
{

        all_events = g_list_reverse (all_events);

        switch (report_type) {
        case REPORT_TYPE_SUMMARY:
                generate_report_summary (uid, seat, session_type);
                break;
        case REPORT_TYPE_LAST:
                generate_report_last (uid, seat, session_type);
                break;
        case REPORT_TYPE_LAST_COMPAT:
                generate_report_last_compat (uid, seat, session_type);
                break;
        case REPORT_TYPE_FREQUENT:
                generate_report_frequent (uid, seat, session_type);
                break;
        case REPORT_TYPE_LOG:
                generate_report_log (uid, seat, session_type);
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
        int                 uid;
        static gboolean     do_version = FALSE;
        static gboolean     report_last_compat = FALSE;
        static gboolean     report_last = FALSE;
        static gboolean     report_frequent = FALSE;
        static gboolean     report_log = FALSE;
        static char        *username = NULL;
        static char        *seat = NULL;
        static char        *session_type = NULL;
        static GOptionEntry entries [] = {
                { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version, N_("Version of this application"), NULL },
                { "frequent", 0, 0, G_OPTION_ARG_NONE, &report_frequent, N_("Show listing of frequent users"), NULL },
                { "last", 0, 0, G_OPTION_ARG_NONE, &report_last, N_("Show listing of last logged in users"), NULL },
                { "last-compat", 0, 0, G_OPTION_ARG_NONE, &report_last_compat, N_("Show 'last' compatible listing of last logged in users"), NULL },
                { "log", 0, 0, G_OPTION_ARG_NONE, &report_log, N_("Show full event log"), NULL },
                { "seat", 's', 0, G_OPTION_ARG_STRING, &seat, N_("Show entries for the specified seat"), N_("SEAT") },
                { "session-type", 't', 0, G_OPTION_ARG_STRING, &session_type, N_("Show entries for the specified session type"), N_("TYPE") },
                { "user", 'u', 0, G_OPTION_ARG_STRING, &username, N_("Show entries for the specified user"), N_("NAME") },
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
        } else if (report_last) {
                report_type = REPORT_TYPE_LAST;
        } else if (report_frequent) {
                report_type = REPORT_TYPE_FREQUENT;
        } else if (report_log) {
                report_type = REPORT_TYPE_LOG;
        } else {
                report_type = REPORT_TYPE_SUMMARY;
        }

        if (username != NULL) {
                uid = get_uid_for_username (username);
                if (uid == -1) {
                        g_warning ("Unknown username: %s", username);
                        exit (1);
                }
        } else {
                uid = -1;
        }

        process_logs ();
        generate_report (report_type, uid, seat, session_type);
        free_events ();

        return 0;
}
