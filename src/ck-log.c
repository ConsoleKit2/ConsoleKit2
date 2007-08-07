/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <syslog.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "ck-log.h"

static gboolean initialized = FALSE;
static int      syslog_levels = (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

static void
log_level_to_priority_and_prefix (GLogLevelFlags log_level,
                                  int           *priorityp,
                                  const char   **prefixp)
{
        int         priority;
        const char *prefix;

        /* Process the message prefix and priority */
        switch (log_level & G_LOG_LEVEL_MASK) {
        case G_LOG_FLAG_FATAL:
                priority = LOG_EMERG;
                prefix = "FATAL";
                break;
        case G_LOG_LEVEL_ERROR:
                priority = LOG_ERR;
                prefix = "ERROR";
                break;
        case G_LOG_LEVEL_CRITICAL:
                priority = LOG_CRIT;
                prefix = "CRITICAL";
                break;
        case G_LOG_LEVEL_WARNING:
                priority = LOG_WARNING;
                prefix = "WARNING";
                break;
        case G_LOG_LEVEL_MESSAGE:
                priority = LOG_NOTICE;
                prefix = "MESSAGE";
                break;
        case G_LOG_LEVEL_INFO:
                priority = LOG_INFO;
                prefix = "INFO";
                break;
        case G_LOG_LEVEL_DEBUG:
                /* if debug was requested then bump this up to ERROR
                 * to ensure it is seen in a log */
                if (syslog_levels & G_LOG_LEVEL_DEBUG) {
                        priority = LOG_WARNING;
                } else {
                        priority = LOG_DEBUG;
                }
                prefix = "DEBUG";
                break;
        default:
                priority = LOG_DEBUG;
                prefix = "UNKNOWN";
                break;
        }

        if (priorityp != NULL) {
                *priorityp = priority;
        }
        if (prefixp != NULL) {
                *prefixp = prefix;
        }
}

void
ck_log_default_handler (const gchar   *log_domain,
                        GLogLevelFlags log_level,
                        const gchar   *message,
                        gpointer       unused_data)
{
        GString     *gstring;
        int          priority;
        const char  *level_prefix;
        char        *string;
        gboolean     do_log;
        gboolean     is_fatal;

        is_fatal = (log_level & G_LOG_FLAG_FATAL) != 0;

        do_log = (log_level & syslog_levels);
        if (! do_log) {
                return;
        }

        if (! initialized) {
                ck_log_init ();
        }

        log_level_to_priority_and_prefix (log_level,
                                          &priority,
                                          &level_prefix);

        gstring = g_string_new (NULL);

        if (log_domain != NULL) {
                g_string_append (gstring, log_domain);
                g_string_append_c (gstring, '-');
        }
        g_string_append (gstring, level_prefix);

        g_string_append (gstring, ": ");
        if (message == NULL) {
                g_string_append (gstring, "(NULL) message");
        } else {
                g_string_append (gstring, message);
        }
        if (is_fatal) {
                g_string_append (gstring, "\naborting...\n");
        } else {
                g_string_append (gstring, "\n");
        }

        string = g_string_free (gstring, FALSE);

        syslog (priority, "%s", string);

        g_free (string);
}

void
ck_log_toggle_debug (void)
{
        if (syslog_levels & G_LOG_LEVEL_DEBUG) {
                g_debug ("Debugging disabled");
                syslog_levels &= ~G_LOG_LEVEL_DEBUG;
        } else {
                syslog_levels |= G_LOG_LEVEL_DEBUG;
                g_debug ("Debugging enabled");
        }
}

void
ck_log_set_debug (gboolean debug)
{
        if (debug) {
                syslog_levels |= G_LOG_LEVEL_DEBUG;
                g_debug ("Debugging enabled");
        } else {
                g_debug ("Debugging disabled");
                syslog_levels &= ~G_LOG_LEVEL_DEBUG;
        }
}

void
ck_log_init (void)
{
        const char *prg_name;
        int         options;

        g_log_set_default_handler (ck_log_default_handler, NULL);

        prg_name = g_get_prgname ();

        options = LOG_PID;
#ifdef LOG_PERROR
        options |= LOG_PERROR;
#endif

        openlog (prg_name, options, LOG_DAEMON);

        initialized = TRUE;
}

void
ck_log_shutdown (void)
{
        closelog ();
        initialized = FALSE;
}

