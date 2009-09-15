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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "ck-log-event.h"

static void
event_seat_added_free (CkLogSeatAddedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
}

static void
event_seat_removed_free (CkLogSeatRemovedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
}

static void
event_system_stop_free (CkLogSystemStopEvent *event)
{
        g_assert (event != NULL);
}

static void
event_system_restart_free (CkLogSystemRestartEvent *event)
{
        g_assert (event != NULL);
}


static void
event_system_start_free (CkLogSystemStartEvent *event)
{
        g_assert (event != NULL);
        g_free (event->kernel_release);
        g_free (event->boot_arguments);
}

static void
event_seat_session_added_free (CkLogSeatSessionAddedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;

        g_free (event->session_id);
        event->session_id = NULL;
        g_free (event->session_type);
        event->session_type = NULL;
        g_free (event->session_x11_display);
        event->session_x11_display = NULL;
        g_free (event->session_x11_display_device);
        event->session_x11_display_device = NULL;
        g_free (event->session_display_device);
        event->session_display_device = NULL;
        g_free (event->session_remote_host_name);
        event->session_remote_host_name = NULL;
        g_free (event->session_creation_time);
        event->session_creation_time = NULL;
}

static void
event_seat_session_removed_free (CkLogSeatSessionRemovedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;

        g_free (event->session_id);
        event->session_id = NULL;
        g_free (event->session_type);
        event->session_type = NULL;
        g_free (event->session_x11_display);
        event->session_x11_display = NULL;
        g_free (event->session_x11_display_device);
        event->session_x11_display_device = NULL;
        g_free (event->session_display_device);
        event->session_display_device = NULL;
        g_free (event->session_remote_host_name);
        event->session_remote_host_name = NULL;
        g_free (event->session_creation_time);
        event->session_creation_time = NULL;
}

static void
event_seat_active_session_changed_free (CkLogSeatActiveSessionChangedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;

        g_free (event->session_id);
        event->session_id = NULL;
}

static void
event_seat_device_added_free (CkLogSeatDeviceAddedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
        g_free (event->device_id);
        event->device_id = NULL;
        g_free (event->device_type);
        event->device_type = NULL;
}

static void
event_seat_device_removed_free (CkLogSeatDeviceRemovedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
        g_free (event->device_id);
        event->device_id = NULL;
        g_free (event->device_type);
        event->device_type = NULL;
}

static void
event_seat_added_copy (CkLogSeatAddedEvent *event,
                       CkLogSeatAddedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->seat_kind = event->seat_kind;
}

static void
event_seat_removed_copy (CkLogSeatRemovedEvent *event,
                         CkLogSeatRemovedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->seat_kind = event->seat_kind;
}

static void
event_system_stop_copy (CkLogSystemStopEvent *event,
                        CkLogSystemStopEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);
}

static void
event_system_restart_copy (CkLogSystemRestartEvent *event,
                           CkLogSystemRestartEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);
}


static void
event_system_start_copy (CkLogSystemStartEvent *event,
                           CkLogSystemStartEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->kernel_release = g_strdup (event->kernel_release);
        event_copy->boot_arguments = g_strdup (event->boot_arguments);
}

static void
event_seat_session_added_copy (CkLogSeatSessionAddedEvent *event,
                               CkLogSeatSessionAddedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->session_id = g_strdup (event->session_id);
        event_copy->session_type = g_strdup (event->session_type);
        event_copy->session_x11_display = g_strdup (event->session_x11_display);
        event_copy->session_x11_display_device = g_strdup (event->session_x11_display_device);
        event_copy->session_display_device = g_strdup (event->session_display_device);
        event_copy->session_remote_host_name = g_strdup (event->session_remote_host_name);
        event_copy->session_is_local = event->session_is_local;
        event_copy->session_unix_user = event->session_unix_user;
        event_copy->session_creation_time = g_strdup (event->session_creation_time);
}

static void
event_seat_session_removed_copy (CkLogSeatSessionRemovedEvent *event,
                                 CkLogSeatSessionRemovedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->session_id = g_strdup (event->session_id);
        event_copy->session_type = g_strdup (event->session_type);
        event_copy->session_x11_display = g_strdup (event->session_x11_display);
        event_copy->session_x11_display_device = g_strdup (event->session_x11_display_device);
        event_copy->session_display_device = g_strdup (event->session_display_device);
        event_copy->session_remote_host_name = g_strdup (event->session_remote_host_name);
        event_copy->session_is_local = event->session_is_local;
        event_copy->session_unix_user = event->session_unix_user;
        event_copy->session_creation_time = g_strdup (event->session_creation_time);
}

static void
event_seat_active_session_changed_copy (CkLogSeatActiveSessionChangedEvent *event,
                                        CkLogSeatActiveSessionChangedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->session_id = g_strdup (event->session_id);
}

static void
event_seat_device_added_copy (CkLogSeatDeviceAddedEvent *event,
                              CkLogSeatDeviceAddedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->device_id = g_strdup (event->device_id);
        event_copy->device_type = g_strdup (event->device_type);
}

static void
event_seat_device_removed_copy (CkLogSeatDeviceRemovedEvent *event,
                                CkLogSeatDeviceRemovedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->device_id = g_strdup (event->device_id);
        event_copy->device_type = g_strdup (event->device_type);
}

CkLogEvent *
ck_log_event_copy (CkLogEvent *event)
{
        CkLogEvent *event_copy;

        if (event == NULL) {
                return NULL;
        }

        event_copy = g_new0 (CkLogEvent, 1);

        event_copy->type = event->type;
        event_copy->timestamp = event->timestamp;

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_ADDED:
                event_seat_added_copy ((CkLogSeatAddedEvent *) event,
                                       (CkLogSeatAddedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_REMOVED:
                event_seat_removed_copy ((CkLogSeatRemovedEvent *) event,
                                         (CkLogSeatRemovedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SYSTEM_STOP:
                event_system_stop_copy ((CkLogSystemStopEvent *) event,
                                        (CkLogSystemStopEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SYSTEM_RESTART:
                event_system_restart_copy ((CkLogSystemRestartEvent *) event,
                                           (CkLogSystemRestartEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                event_system_start_copy ((CkLogSystemStartEvent *) event,
                                           (CkLogSystemStartEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                event_seat_session_added_copy ((CkLogSeatSessionAddedEvent *) event,
                                               (CkLogSeatSessionAddedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_REMOVED:
                event_seat_session_removed_copy ((CkLogSeatSessionRemovedEvent *) event,
                                                 (CkLogSeatSessionRemovedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_ADDED:
                event_seat_device_added_copy ((CkLogSeatDeviceAddedEvent *) event,
                                              (CkLogSeatDeviceAddedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_REMOVED:
                event_seat_device_removed_copy ((CkLogSeatDeviceRemovedEvent *) event,
                                                (CkLogSeatDeviceRemovedEvent *) event_copy);
                break;
        case CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                event_seat_active_session_changed_copy ((CkLogSeatActiveSessionChangedEvent *) event,
                                                        (CkLogSeatActiveSessionChangedEvent *) event_copy);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return event_copy;
}

void
ck_log_event_free (CkLogEvent *event)
{
        switch (event->type) {
        case CK_LOG_EVENT_SEAT_ADDED:
                event_seat_added_free ((CkLogSeatAddedEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_REMOVED:
                event_seat_removed_free ((CkLogSeatRemovedEvent *) event);
                break;
        case CK_LOG_EVENT_SYSTEM_STOP:
                event_system_stop_free ((CkLogSystemStopEvent *) event);
                break;
        case CK_LOG_EVENT_SYSTEM_RESTART:
                event_system_restart_free ((CkLogSystemRestartEvent *) event);
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                event_system_start_free ((CkLogSystemStartEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                event_seat_session_added_free ((CkLogSeatSessionAddedEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_REMOVED:
                event_seat_session_removed_free ((CkLogSeatSessionRemovedEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_ADDED:
                event_seat_device_added_free ((CkLogSeatDeviceAddedEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_REMOVED:
                event_seat_device_removed_free ((CkLogSeatDeviceRemovedEvent *) event);
                break;
        case CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                event_seat_active_session_changed_free ((CkLogSeatActiveSessionChangedEvent *) event);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_free (event);
}

static void
add_log_for_seat_added (GString    *str,
                        CkLogEvent *event)
{
        CkLogSeatAddedEvent *e;

        e = (CkLogSeatAddedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' seat-kind=%d",
                                e->seat_id,
                                e->seat_kind);
}

static void
add_log_for_seat_removed (GString    *str,
                          CkLogEvent *event)
{
        CkLogSeatRemovedEvent *e;

        e = (CkLogSeatRemovedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' seat-kind=%d",
                                e->seat_id,
                                e->seat_kind);
}

static void
add_log_for_seat_session_added (GString    *str,
                                CkLogEvent *event)
{
        CkLogSeatSessionAddedEvent *e;

        e = (CkLogSeatSessionAddedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' session-id='%s' session-type='%s' session-x11-display='%s' session-x11-display-device='%s' session-display-device='%s' session-remote-host-name='%s' session-is-local=%s session-unix-user=%u session-creation-time='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->session_id ? e->session_id : "",
                                e->session_type ? e->session_type : "",
                                e->session_x11_display ? e->session_x11_display : "",
                                e->session_x11_display_device ? e->session_x11_display_device : "",
                                e->session_display_device ? e->session_display_device : "",
                                e->session_remote_host_name ? e->session_remote_host_name : "",
                                e->session_is_local ? "TRUE" : "FALSE",
                                e->session_unix_user,
                                e->session_creation_time ? e->session_creation_time : "");
}

static void
add_log_for_seat_session_removed (GString    *str,
                                  CkLogEvent *event)
{
        CkLogSeatSessionRemovedEvent *e;

        e = (CkLogSeatSessionRemovedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' session-id='%s' session-type='%s' session-x11-display='%s' session-x11-display-device='%s' session-display-device='%s' session-remote-host-name='%s' session-is-local=%s session-unix-user=%u session-creation-time='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->session_id ? e->session_id : "",
                                e->session_type ? e->session_type : "",
                                e->session_x11_display ? e->session_x11_display : "",
                                e->session_x11_display_device ? e->session_x11_display_device : "",
                                e->session_display_device ? e->session_display_device : "",
                                e->session_remote_host_name ? e->session_remote_host_name : "",
                                e->session_is_local ? "TRUE" : "FALSE",
                                e->session_unix_user,
                                e->session_creation_time ? e->session_creation_time : "");
}

static void
add_log_for_system_stop (GString    *str,
                         CkLogEvent *event)
{
        CkLogSystemStopEvent *e;

        e = (CkLogSystemStopEvent *)event;
}

static void
add_log_for_system_restart (GString    *str,
                            CkLogEvent *event)
{
        CkLogSystemRestartEvent *e;

        e = (CkLogSystemRestartEvent *)event;
}


static void
add_log_for_system_start (GString    *str,
                          CkLogEvent *event)
{
        CkLogSystemStartEvent *e;

        e = (CkLogSystemStartEvent *)event;
        g_string_append_printf (str,
                                "kernel-release='%s' boot-arguments='%s'",
                                e->kernel_release ? e->kernel_release : "",
                                e->boot_arguments ? e->boot_arguments : "");
}

static void
add_log_for_seat_active_session_changed (GString    *str,
                                         CkLogEvent *event)
{
        CkLogSeatActiveSessionChangedEvent *e;

        e = (CkLogSeatActiveSessionChangedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' session-id='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->session_id ? e->session_id : "");
}

static void
add_log_for_seat_device_added (GString    *str,
                               CkLogEvent *event)
{
        CkLogSeatDeviceAddedEvent *e;

        e = (CkLogSeatDeviceAddedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' device-id='%s' device-type='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->device_id ? e->device_id : "",
                                e->device_type ? e->device_type : "");
}

static void
add_log_for_seat_device_removed (GString    *str,
                                 CkLogEvent *event)
{
        CkLogSeatDeviceRemovedEvent *e;

        e = (CkLogSeatDeviceRemovedEvent *)event;
        g_string_append_printf (str,
                                "seat-id='%s' device-id='%s' device-type='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->device_id ? e->device_id : "",
                                e->device_type ? e->device_type : "");
}

static const char *
event_type_to_name (CkLogEventType event_type)
{
        const char *str;
        switch (event_type) {
        case CK_LOG_EVENT_SEAT_ADDED:
                str = "SEAT_ADDED";
                break;
        case CK_LOG_EVENT_SEAT_REMOVED:
                str = "SEAT_REMOVED";
                break;
        case CK_LOG_EVENT_SYSTEM_STOP:
                str = "SYSTEM_STOP";
                break;
        case CK_LOG_EVENT_SYSTEM_RESTART:
                str = "SYSTEM_RESTART";
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                str = "SYSTEM_START";
                break;
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                str = "SEAT_SESSION_ADDED";
                break;
        case CK_LOG_EVENT_SEAT_SESSION_REMOVED:
                str = "SEAT_SESSION_REMOVED";
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_ADDED:
                str = "SEAT_DEVICE_ADDED";
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_REMOVED:
                str = "SEAT_DEVICE_REMOVED";
                break;
        case CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                str = "SEAT_ACTIVE_SESSION_CHANGED";
                break;
        default:
                str = "UNKNOWN";
                break;
        }
        return str;
}

static gboolean
event_name_to_type (const char     *event_name,
                    CkLogEventType *event_type)
{
        gboolean ret;

        ret = TRUE;

        if (strcmp (event_name, "SEAT_ADDED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_ADDED;
        } else if (strcmp (event_name, "SEAT_REMOVED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_REMOVED;
        } else if (strcmp (event_name, "SYSTEM_STOP") == 0) {
                *event_type = CK_LOG_EVENT_SYSTEM_STOP;
        } else if (strcmp (event_name, "SYSTEM_RESTART") == 0) {
                *event_type = CK_LOG_EVENT_SYSTEM_RESTART;
        } else if (strcmp (event_name, "SYSTEM_START") == 0) {
                *event_type = CK_LOG_EVENT_SYSTEM_START;
        } else if (strcmp (event_name, "SEAT_SESSION_ADDED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_SESSION_ADDED;
        } else if (strcmp (event_name, "SEAT_SESSION_REMOVED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_SESSION_REMOVED;
        } else if (strcmp (event_name, "SEAT_DEVICE_ADDED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_DEVICE_ADDED;
        } else if (strcmp (event_name, "SEAT_DEVICE_REMOVED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_DEVICE_REMOVED;
        } else if (strcmp (event_name, "SEAT_ACTIVE_SESSION_CHANGED") == 0) {
                *event_type = CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED;
        } else {
                ret = FALSE;
        }

        return ret;
}

static void
add_log_for_any (GString    *str,
                 CkLogEvent *event)
{
        g_string_append_printf (str,
                                "%lu.%03u type=%s : ",
                                (gulong)event->timestamp.tv_sec,
                                (guint)(event->timestamp.tv_usec / 1000),
                                event_type_to_name (event->type));
}

void
ck_log_event_to_string (CkLogEvent  *event,
                        GString     *str)
{

        add_log_for_any (str, event);

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_ADDED:
                add_log_for_seat_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_REMOVED:
                add_log_for_seat_removed (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_STOP:
                add_log_for_system_stop (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_RESTART:
                add_log_for_system_restart (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                add_log_for_system_start (str, event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                add_log_for_seat_session_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_REMOVED:
                add_log_for_seat_session_removed (str, event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_ADDED:
                add_log_for_seat_device_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_REMOVED:
                add_log_for_seat_device_removed (str, event);
                break;
        case CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                add_log_for_seat_active_session_changed (str, event);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

static const char *
skip_header (const char *str,
             gssize      len)
{
        char *r;
        r = g_strstr_len (str,
                          len,
                          " : ");
        if (r != NULL) {
                r += 3;
                goto out;
        }

        r = g_strstr_len (str,
                          len,
                          " :");
        if (r != NULL) {
                r += 2;
        }

 out:
        return r;
}

static gboolean
parse_value_as_ulong (const char *value,
                      gulong     *intval)
{
        char *end_of_valid_int;
        glong ulong_value;

        ulong_value = strtoul (value, &end_of_valid_int, 10);

        if (*value == '\0' || *end_of_valid_int != '\0') {
                return FALSE;
        }

        *intval = ulong_value;

        return TRUE;
}

static gboolean
parse_log_for_seat_added (const GString *str,
                          CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        char       *tmp;
        CkLogSeatAddedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' seat-kind=(?P<sessionid>[0-9]*)", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse seat added event: %s", s);
                goto out;
        }

        e = (CkLogSeatAddedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");

        tmp = g_match_info_fetch_named (match_info, "seatkind");
        if (tmp != NULL) {
                gulong l;
                if (parse_value_as_ulong (tmp, &l)) {
                        e->seat_kind = l;
                }
        }

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_removed (const GString *str,
                            CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        char       *tmp;
        CkLogSeatRemovedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' seat-kind=(?P<sessionid>[0-9]*)", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse seat removed event: %s", s);
                goto out;
        }

        e = (CkLogSeatRemovedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");

        tmp = g_match_info_fetch_named (match_info, "seatkind");
        if (tmp != NULL) {
                gulong l;
                if (parse_value_as_ulong (tmp, &l)) {
                        e->seat_kind = l;
                }
        }

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_system_stop (const GString *str,
                           CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        CkLogSystemStopEvent *e;

        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        e = (CkLogSystemStopEvent *)event;

        ret = TRUE;
 out:

        return ret;
}

static gboolean
parse_log_for_system_restart (const GString *str,
                              CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        CkLogSystemRestartEvent *e;

        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        e = (CkLogSystemRestartEvent *)event;

        ret = TRUE;
 out:

        return ret;
}

static gboolean
parse_log_for_system_start (const GString *str,
                            CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        CkLogSystemStartEvent *e;

        ret = FALSE;
        re = NULL;
        match_info = NULL;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        /* kernel-release and boot-arguments are attributes added in 0.4 */
        error = NULL;
        re = g_regex_new ("(kernel-release='(?P<release>[^']+)')?[ ]?(boot-arguments='(?P<arguments>.*)')?", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse system start event: %s", s);
                goto out;
        }

        e = (CkLogSystemStartEvent *)event;

        e->kernel_release = g_match_info_fetch_named (match_info, "release");
        e->boot_arguments = g_match_info_fetch_named (match_info, "arguments");

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_session_added (const GString *str,
                                  CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        char       *tmp;
        CkLogSeatSessionAddedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' session-id='(?P<sessionid>[a-zA-Z0-9/]+)' session-type='(?P<sessiontype>[a-zA-Z0-9 ]*)' session-x11-display='(?P<sessionx11display>[0-9a-zA-Z.:]*)' session-x11-display-device='(?P<sessionx11displaydevice>[^']*)' session-display-device='(?P<sessiondisplaydevice>[^']*)' session-remote-host-name='(?P<sessionremovehostname>[^']*)' session-is-local=(?P<sessionislocal>[a-zA-Z]*) session-unix-user=(?P<sessionunixuser>[0-9]*) session-creation-time='(?P<sessioncreationtime>[^']*)'", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse session added event: %s", s);
                goto out;
        }

        e = (CkLogSeatSessionAddedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");
        e->session_id = g_match_info_fetch_named (match_info, "sessionid");
        e->session_type = g_match_info_fetch_named (match_info, "sessiontype");
        e->session_x11_display = g_match_info_fetch_named (match_info, "sessionx11display");
        e->session_x11_display_device = g_match_info_fetch_named (match_info, "sessionx11displaydevice");
        e->session_display_device = g_match_info_fetch_named (match_info, "sessiondisplaydevice");
        e->session_remote_host_name = g_match_info_fetch_named (match_info, "sessionremotehostname");
        e->session_creation_time = g_match_info_fetch_named (match_info, "sessioncreationtime");

        tmp = g_match_info_fetch_named (match_info, "sessionislocal");
        if (tmp != NULL && strcmp (tmp, "TRUE") == 0) {
                e->session_is_local = TRUE;
        } else {
                e->session_is_local = FALSE;
        }
        g_free (tmp);

        tmp = g_match_info_fetch_named (match_info, "sessionunixuser");
        if (tmp != NULL) {
                gulong l;
                if (parse_value_as_ulong (tmp, &l)) {
                        e->session_unix_user = l;
                }
        }

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_session_removed (const GString *str,
                                    CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        char       *tmp;
        CkLogSeatSessionRemovedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' session-id='(?P<sessionid>[a-zA-Z0-9/]+)' session-type='(?P<sessiontype>[a-zA-Z0-9 ]*)' session-x11-display='(?P<sessionx11display>[0-9a-zA-Z.:]*)' session-x11-display-device='(?P<sessionx11displaydevice>[^']*)' session-display-device='(?P<sessiondisplaydevice>[^']*)' session-remote-host-name='(?P<sessionremovehostname>[^']*)' session-is-local=(?P<sessionislocal>[a-zA-Z]*) session-unix-user=(?P<sessionunixuser>[0-9]*) session-creation-time='(?P<sessioncreationtime>[^']*)'", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse session removed event: %s", s);
                goto out;
        }

        e = (CkLogSeatSessionRemovedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");
        e->session_id = g_match_info_fetch_named (match_info, "sessionid");
        e->session_type = g_match_info_fetch_named (match_info, "sessiontype");
        e->session_x11_display = g_match_info_fetch_named (match_info, "sessionx11display");
        e->session_x11_display_device = g_match_info_fetch_named (match_info, "sessionx11displaydevice");
        e->session_display_device = g_match_info_fetch_named (match_info, "sessiondisplaydevice");
        e->session_remote_host_name = g_match_info_fetch_named (match_info, "sessionremotehostname");
        e->session_creation_time = g_match_info_fetch_named (match_info, "sessioncreationtime");

        tmp = g_match_info_fetch_named (match_info, "sessionislocal");
        if (tmp != NULL && strcmp (tmp, "TRUE") == 0) {
                e->session_is_local = TRUE;
        } else {
                e->session_is_local = FALSE;
        }
        g_free (tmp);

        tmp = g_match_info_fetch_named (match_info, "sessionunixuser");
        if (tmp != NULL) {
                gulong l;
                if (parse_value_as_ulong (tmp, &l)) {
                        e->session_unix_user = l;
                }
        }

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_active_session_changed (const GString *str,
                                           CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        CkLogSeatActiveSessionChangedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' session-id='(?P<sessionid>[a-zA-Z0-9/]*)'", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse session changed event: %s", s);
                goto out;
        }

        e = (CkLogSeatActiveSessionChangedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");
        e->session_id = g_match_info_fetch_named (match_info, "sessionid");

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_device_added (const GString *str,
                                 CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        CkLogSeatDeviceAddedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' device-id='(?P<deviceid>[^']+)' device-type='(?P<devicetype>[^']+)'", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse device added event: %s", s);
                goto out;
        }

        e = (CkLogSeatDeviceAddedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");
        e->device_id = g_match_info_fetch_named (match_info, "deviceid");
        e->device_type = g_match_info_fetch_named (match_info, "devicetype");

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_seat_device_removed (const GString *str,
                                   CkLogEvent    *event)
{
        gboolean    ret;
        const char *s;
        GRegex     *re;
        GMatchInfo *match_info;
        gboolean    res;
        GError     *error;
        CkLogSeatDeviceRemovedEvent *e;

        re = NULL;
        match_info = NULL;
        ret = FALSE;

        s = skip_header (str->str, str->len);
        if (s == NULL) {
                goto out;
        }

        error = NULL;
        re = g_regex_new ("seat-id='(?P<seatid>[a-zA-Z0-9/]+)' device-id='(?P<deviceid>[^']+)' device-type='(?P<devicetype>[^']+)'", 0, 0, &error);
        if (re == NULL) {
                g_warning (error->message);
                goto out;
        }

        g_regex_match (re, s, 0, &match_info);

        res = g_match_info_matches (match_info);
        if (! res) {
                g_warning ("Unable to parse device removed event: %s", s);
                goto out;
        }

        e = (CkLogSeatDeviceRemovedEvent *)event;
        e->seat_id = g_match_info_fetch_named (match_info, "seatid");
        e->device_id = g_match_info_fetch_named (match_info, "deviceid");
        e->device_type = g_match_info_fetch_named (match_info, "devicetype");

        ret = TRUE;
 out:
        if (match_info != NULL) {
                g_match_info_free (match_info);
        }
        if (re != NULL) {
                g_regex_unref (re);
        }

        return ret;
}

static gboolean
parse_log_for_any (const GString *str,
                   CkLogEvent    *event)
{
        gboolean ret;
        int      res;
        gulong   sec;
        guint    frac;
        char     buf[32];

        ret = FALSE;

        res = sscanf (str->str, "%lu.%u type=%30s :",
                      &sec,
                      &frac,
                      buf);
        if (res == 3) {
                res = event_name_to_type (buf, &event->type);
                if (! res) {
                        goto out;
                }

                event->timestamp.tv_sec = sec;
                event->timestamp.tv_usec = 1000 * frac;

                ret = TRUE;
        }
 out:
        return ret;
}

gboolean
ck_log_event_fill_from_string (CkLogEvent    *event,
                               const GString *str)
{
        gboolean res;
        gboolean ret;

        g_return_val_if_fail (str != NULL, FALSE);

        ret = FALSE;

        res = parse_log_for_any (str, event);
        if (! res) {
                goto out;
        }

        switch (event->type) {
        case CK_LOG_EVENT_SEAT_ADDED:
                res = parse_log_for_seat_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_REMOVED:
                res = parse_log_for_seat_removed (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_STOP:
                res = parse_log_for_system_stop (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_RESTART:
                res = parse_log_for_system_restart (str, event);
                break;
        case CK_LOG_EVENT_SYSTEM_START:
                res = parse_log_for_system_start (str, event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_ADDED:
                res = parse_log_for_seat_session_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_SESSION_REMOVED:
                res = parse_log_for_seat_session_removed (str, event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_ADDED:
                res = parse_log_for_seat_device_added (str, event);
                break;
        case CK_LOG_EVENT_SEAT_DEVICE_REMOVED:
                res = parse_log_for_seat_device_removed (str, event);
                break;
        case CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                res = parse_log_for_seat_active_session_changed (str, event);
                break;
        default:
                g_assert_not_reached ();
                break;
        }
        if (! res) {
                goto out;
        }
        ret = TRUE;
 out:
        return ret;
}

CkLogEvent *
ck_log_event_new_from_string (const GString *str)
{
        CkLogEvent *event;
        gboolean    res;

        g_return_val_if_fail (str != NULL, NULL);

        event = g_new0 (CkLogEvent, 1);
        res = ck_log_event_fill_from_string (event, str);
        if (! res) {
                g_free (event);
                event = NULL;
        }

        return event;
}
