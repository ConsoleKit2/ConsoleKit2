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

#ifndef __CK_EVENT_LOGGER_H
#define __CK_EVENT_LOGGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CK_TYPE_EVENT_LOGGER         (ck_event_logger_get_type ())
#define CK_EVENT_LOGGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_EVENT_LOGGER, CkEventLogger))
#define CK_EVENT_LOGGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_EVENT_LOGGER, CkEventLoggerClass))
#define CK_IS_EVENT_LOGGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_EVENT_LOGGER))
#define CK_IS_EVENT_LOGGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_EVENT_LOGGER))
#define CK_EVENT_LOGGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_EVENT_LOGGER, CkEventLoggerClass))

typedef struct CkEventLoggerPrivate CkEventLoggerPrivate;

typedef struct
{
        GObject               parent;
        CkEventLoggerPrivate *priv;
} CkEventLogger;

typedef struct
{
        GObjectClass   parent_class;
} CkEventLoggerClass;

typedef enum
{
        CK_EVENT_LOGGER_EVENT_START = 0,
        CK_EVENT_LOGGER_EVENT_STOP,
        CK_EVENT_LOGGER_EVENT_SYSTEM_SHUTDOWN,
        CK_EVENT_LOGGER_EVENT_SYSTEM_RUNLEVEL_CHANGED,
        CK_EVENT_LOGGER_EVENT_SEAT_ADDED,
        CK_EVENT_LOGGER_EVENT_SEAT_REMOVED,
        CK_EVENT_LOGGER_EVENT_SEAT_SESSION_ADDED,
        CK_EVENT_LOGGER_EVENT_SEAT_SESSION_REMOVED,
        CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_ADDED,
        CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_REMOVED,
        CK_EVENT_LOGGER_EVENT_SEAT_ACTIVE_SESSION_CHANGED,
} CkEventLoggerEventType;

typedef struct
{
        char *seat_id;
        int   seat_kind;
} CkEventLoggerSeatAddedEvent;

typedef struct
{
        char *seat_id;
        int   seat_kind;
} CkEventLoggerSeatRemovedEvent;

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
} CkEventLoggerSeatSessionAddedEvent;

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
} CkEventLoggerSeatSessionRemovedEvent;

typedef struct
{
        char    *seat_id;
        char    *session_id;
} CkEventLoggerSeatActiveSessionChangedEvent;

typedef struct
{
        char *seat_id;
        char *device_type;
        char *device_id;
} CkEventLoggerSeatDeviceAddedEvent;

typedef struct
{
        char *seat_id;
        char *device_type;
        char *device_id;
} CkEventLoggerSeatDeviceRemovedEvent;

typedef struct
{
        union {
                CkEventLoggerSeatAddedEvent seat_added;
                CkEventLoggerSeatRemovedEvent seat_removed;
                CkEventLoggerSeatSessionAddedEvent seat_session_added;
                CkEventLoggerSeatSessionRemovedEvent seat_session_removed;
                CkEventLoggerSeatActiveSessionChangedEvent seat_active_session_changed;
                CkEventLoggerSeatDeviceAddedEvent seat_device_added;
                CkEventLoggerSeatDeviceRemovedEvent seat_device_removed;
        } event;

        GTimeVal               timestamp;
        CkEventLoggerEventType type;
} CkEventLoggerEvent;


typedef enum
{
         CK_EVENT_LOGGER_ERROR_GENERAL
} CkEventLoggerError;

#define CK_EVENT_LOGGER_ERROR ck_event_logger_error_quark ()

GQuark               ck_event_logger_error_quark         (void);
GType                ck_event_logger_get_type            (void);
CkEventLogger      * ck_event_logger_new                 (const char         *filename);

gboolean             ck_event_logger_queue_event         (CkEventLogger      *event_logger,
                                                          CkEventLoggerEvent *event,
                                                          GError            **error);

CkEventLoggerEvent * ck_event_logger_event_copy          (CkEventLoggerEvent *event);
void                 ck_event_logger_event_free          (CkEventLoggerEvent *event);


G_END_DECLS

#endif /* __CK_EVENT_LOGGER_H */
