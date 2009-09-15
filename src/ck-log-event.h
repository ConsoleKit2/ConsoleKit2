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

#ifndef __CK_LOG_EVENT_H
#define __CK_LOG_EVENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
        CK_LOG_EVENT_NONE = 0,
        CK_LOG_EVENT_START,
        CK_LOG_EVENT_STOP,
        CK_LOG_EVENT_SYSTEM_START,
        CK_LOG_EVENT_SYSTEM_STOP,
        CK_LOG_EVENT_SYSTEM_RESTART,
        CK_LOG_EVENT_SYSTEM_RUNLEVEL_CHANGED,
        CK_LOG_EVENT_SEAT_ADDED,
        CK_LOG_EVENT_SEAT_REMOVED,
        CK_LOG_EVENT_SEAT_SESSION_ADDED,
        CK_LOG_EVENT_SEAT_SESSION_REMOVED,
        CK_LOG_EVENT_SEAT_DEVICE_ADDED,
        CK_LOG_EVENT_SEAT_DEVICE_REMOVED,
        CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED,
} CkLogEventType;

typedef struct
{
        gpointer dummy;
} CkLogNoneEvent;

typedef struct
{
        gpointer dummy;
} CkLogSystemStopEvent;

typedef struct
{
        gpointer dummy;
} CkLogSystemRestartEvent;

typedef struct
{
        char *kernel_release;
        char *boot_arguments;
} CkLogSystemStartEvent;

typedef struct
{
        char *seat_id;
        int   seat_kind;
} CkLogSeatAddedEvent;

typedef struct
{
        char *seat_id;
        int   seat_kind;
} CkLogSeatRemovedEvent;

typedef struct
{
        char    *seat_id;
        char    *session_id;
        char    *session_type;
        char    *session_x11_display;
        char    *session_x11_display_device;
        char    *session_display_device;
        char    *session_remote_host_name;
        gboolean session_is_local;
        guint    session_unix_user;
        char    *session_creation_time;
} CkLogSeatSessionAddedEvent;

typedef struct
{
        char    *seat_id;
        char    *session_id;
        char    *session_type;
        char    *session_x11_display;
        char    *session_x11_display_device;
        char    *session_display_device;
        char    *session_remote_host_name;
        gboolean session_is_local;
        guint    session_unix_user;
        char    *session_creation_time;
} CkLogSeatSessionRemovedEvent;

typedef struct
{
        char    *seat_id;
        char    *session_id;
} CkLogSeatActiveSessionChangedEvent;

typedef struct
{
        char *seat_id;
        char *device_type;
        char *device_id;
} CkLogSeatDeviceAddedEvent;

typedef struct
{
        char *seat_id;
        char *device_type;
        char *device_id;
} CkLogSeatDeviceRemovedEvent;

typedef struct
{
        union {
                CkLogNoneEvent none;
                CkLogSystemRestartEvent system_start;
                CkLogSystemStopEvent system_stop;
                CkLogSystemRestartEvent system_restart;
                CkLogSeatAddedEvent seat_added;
                CkLogSeatRemovedEvent seat_removed;
                CkLogSeatSessionAddedEvent seat_session_added;
                CkLogSeatSessionRemovedEvent seat_session_removed;
                CkLogSeatActiveSessionChangedEvent seat_active_session_changed;
                CkLogSeatDeviceAddedEvent seat_device_added;
                CkLogSeatDeviceRemovedEvent seat_device_removed;
        } event;

        GTimeVal       timestamp;
        CkLogEventType type;
} CkLogEvent;

CkLogEvent         * ck_log_event_copy             (CkLogEvent    *event);
void                 ck_log_event_free             (CkLogEvent    *event);

CkLogEvent         * ck_log_event_new_from_string  (const GString *str);
gboolean             ck_log_event_fill_from_string (CkLogEvent    *event,
                                                    const GString *str);

void                 ck_log_event_to_string        (CkLogEvent    *event,
                                                    GString       *str);

G_END_DECLS

#endif /* __CK_LOG_EVENT_H */
