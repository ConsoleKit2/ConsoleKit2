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

#define _GNU_SOURCE             /* For O_NOFOLLOW */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "ck-event-logger.h"

#define CK_EVENT_LOGGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_EVENT_LOGGER, CkEventLoggerPrivate))

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/run/ConsoleKit/history"

struct CkEventLoggerPrivate
{
        int              fd;
        FILE            *file;
        GThread         *writer_thread;
        GAsyncQueue     *event_queue;
        char            *log_filename;
};

enum {
        PROP_0,
        PROP_LOG_FILENAME
};

static void     ck_event_logger_class_init  (CkEventLoggerClass *klass);
static void     ck_event_logger_init        (CkEventLogger      *event_logger);
static void     ck_event_logger_finalize    (GObject            *object);

G_DEFINE_TYPE (CkEventLogger, ck_event_logger, G_TYPE_OBJECT)

GQuark
ck_event_logger_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_event_logger_error");
        }

        return ret;
}

static void
event_seat_added_free (CkEventLoggerSeatAddedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
}

static void
event_seat_removed_free (CkEventLoggerSeatRemovedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;
}

static void
event_seat_session_added_free (CkEventLoggerSeatSessionAddedEvent *event)
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
event_seat_session_removed_free (CkEventLoggerSeatSessionRemovedEvent *event)
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
event_seat_active_session_changed_free (CkEventLoggerSeatActiveSessionChangedEvent *event)
{
        g_assert (event != NULL);

        g_free (event->seat_id);
        event->seat_id = NULL;

        g_free (event->session_id);
        event->session_id = NULL;
}

static void
event_seat_device_added_free (CkEventLoggerSeatDeviceAddedEvent *event)
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
event_seat_device_removed_free (CkEventLoggerSeatDeviceRemovedEvent *event)
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
event_seat_added_copy (CkEventLoggerSeatAddedEvent *event,
                       CkEventLoggerSeatAddedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->seat_kind = event->seat_kind;
}

static void
event_seat_removed_copy (CkEventLoggerSeatRemovedEvent *event,
                         CkEventLoggerSeatRemovedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->seat_kind = event->seat_kind;
}

static void
event_seat_session_added_copy (CkEventLoggerSeatSessionAddedEvent *event,
                               CkEventLoggerSeatSessionAddedEvent *event_copy)
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
event_seat_session_removed_copy (CkEventLoggerSeatSessionRemovedEvent *event,
                                 CkEventLoggerSeatSessionRemovedEvent *event_copy)
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
event_seat_active_session_changed_copy (CkEventLoggerSeatActiveSessionChangedEvent *event,
                                        CkEventLoggerSeatActiveSessionChangedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->session_id = g_strdup (event->session_id);
}

static void
event_seat_device_added_copy (CkEventLoggerSeatDeviceAddedEvent *event,
                              CkEventLoggerSeatDeviceAddedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->device_id = g_strdup (event->device_id);
        event_copy->device_type = g_strdup (event->device_type);
}

static void
event_seat_device_removed_copy (CkEventLoggerSeatDeviceRemovedEvent *event,
                                CkEventLoggerSeatDeviceRemovedEvent *event_copy)
{
        g_assert (event != NULL);
        g_assert (event_copy != NULL);

        event_copy->seat_id = g_strdup (event->seat_id);
        event_copy->device_id = g_strdup (event->device_id);
        event_copy->device_type = g_strdup (event->device_type);
}

CkEventLoggerEvent *
ck_event_logger_event_copy (CkEventLoggerEvent *event)
{
        CkEventLoggerEvent *event_copy;

        if (event == NULL) {
                return NULL;
        }

        event_copy = g_new0 (CkEventLoggerEvent, 1);

        event_copy->type = event->type;
        event_copy->timestamp = event->timestamp;

        switch (event->type) {
        case CK_EVENT_LOGGER_EVENT_SEAT_ADDED:
                event_seat_added_copy ((CkEventLoggerSeatAddedEvent *) event,
                                       (CkEventLoggerSeatAddedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_REMOVED:
                event_seat_removed_copy ((CkEventLoggerSeatRemovedEvent *) event,
                                         (CkEventLoggerSeatRemovedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_ADDED:
                event_seat_session_added_copy ((CkEventLoggerSeatSessionAddedEvent *) event,
                                               (CkEventLoggerSeatSessionAddedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_REMOVED:
                event_seat_session_removed_copy ((CkEventLoggerSeatSessionRemovedEvent *) event,
                                                 (CkEventLoggerSeatSessionRemovedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_ADDED:
                event_seat_device_added_copy ((CkEventLoggerSeatDeviceAddedEvent *) event,
                                              (CkEventLoggerSeatDeviceAddedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_REMOVED:
                event_seat_device_removed_copy ((CkEventLoggerSeatDeviceRemovedEvent *) event,
                                                (CkEventLoggerSeatDeviceRemovedEvent *) event_copy);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                event_seat_active_session_changed_copy ((CkEventLoggerSeatActiveSessionChangedEvent *) event,
                                                        (CkEventLoggerSeatActiveSessionChangedEvent *) event_copy);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return event_copy;
}

void
ck_event_logger_event_free (CkEventLoggerEvent *event)
{
        switch (event->type) {
        case CK_EVENT_LOGGER_EVENT_SEAT_ADDED:
                event_seat_added_free ((CkEventLoggerSeatAddedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_REMOVED:
                event_seat_removed_free ((CkEventLoggerSeatRemovedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_ADDED:
                event_seat_session_added_free ((CkEventLoggerSeatSessionAddedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_REMOVED:
                event_seat_session_removed_free ((CkEventLoggerSeatSessionRemovedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_ADDED:
                event_seat_device_added_free ((CkEventLoggerSeatDeviceAddedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_REMOVED:
                event_seat_device_removed_free ((CkEventLoggerSeatDeviceRemovedEvent *) event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                event_seat_active_session_changed_free ((CkEventLoggerSeatActiveSessionChangedEvent *) event);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_free (event);
}

gboolean
ck_event_logger_queue_event (CkEventLogger      *event_logger,
                             CkEventLoggerEvent *event,
                             GError            **error)
{
        CkEventLoggerEvent *event_copy;
        gboolean            ret;

        g_return_val_if_fail (CK_IS_EVENT_LOGGER (event_logger), FALSE);
        g_return_val_if_fail (event != NULL, FALSE);

        event_copy = ck_event_logger_event_copy (event);

        g_async_queue_push (event_logger->priv->event_queue,
                            event_copy);

        ret = TRUE;

        return ret;
}

/* Adapted from auditd auditd-event.c */
static gboolean
open_log_file (CkEventLogger *event_logger)
{
        int flags;
        int fd;

        /*
         * Likely errors on rotate: ENFILE, ENOMEM, ENOSPC
         */
        flags = O_WRONLY | O_APPEND;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif

retry:
        fd = g_open (event_logger->priv->log_filename, flags, 0600);
        if (fd < 0) {
                if (errno == ENOENT) {
                        /* FIXME: should we just skip if file doesn't exist? */
                        fd = g_open (event_logger->priv->log_filename,
                                     O_CREAT | O_EXCL | O_APPEND,
                                     S_IRUSR | S_IWUSR | S_IRGRP);
                        if (fd < 0) {
                                g_warning ("Couldn't create log file %s (%s)",
                                           event_logger->priv->log_filename,
                                           g_strerror (errno));
                                return FALSE;
                        }

                        close (fd);
                        fd = g_open (event_logger->priv->log_filename, flags, 0600);
                } else if (errno == ENFILE) {
                        /* All system descriptors used, try again... */
                        goto retry;
                }
                if (fd < 0) {
                        g_warning ("Couldn't open log file %s (%s)",
                                   event_logger->priv->log_filename,
                                   g_strerror (errno));
                        return FALSE;
                }
        }

        if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
                close (fd);
                g_warning ("Error setting log file CLOEXEC flag (%s)",
                           g_strerror (errno));
                return FALSE;
        }

        fchown (fd, 0, 0);

        event_logger->priv->fd = fd;
        event_logger->priv->file = fdopen (fd, "a");

        if (event_logger->priv->file == NULL) {
                g_warning ("Error setting up log descriptor (%s)",
                           g_strerror (errno));
                close (fd);
                return FALSE;
        }

        /* Set it to line buffering */
        setlinebuf (event_logger->priv->file);

        return TRUE;
}

static void
add_log_for_seat_added (GString            *str,
                        CkEventLoggerEvent *event)
{
        CkEventLoggerSeatAddedEvent *e;

        e = (CkEventLoggerSeatAddedEvent *)event;
        g_string_append_printf (str,
                                " seat-id=%s seat-kind=%d",
                                e->seat_id,
                                e->seat_kind);
}

static void
add_log_for_seat_removed (GString            *str,
                          CkEventLoggerEvent *event)
{
        CkEventLoggerSeatRemovedEvent *e;

        e = (CkEventLoggerSeatRemovedEvent *)event;
        g_string_append_printf (str,
                                " seat-id=%s seat-kind=%d",
                                e->seat_id,
                                e->seat_kind);
}

static void
add_log_for_seat_session_added (GString            *str,
                                CkEventLoggerEvent *event)
{
        CkEventLoggerSeatSessionAddedEvent *e;

        e = (CkEventLoggerSeatSessionAddedEvent *)event;
        g_string_append_printf (str,
                                " seat-id='%s' session-id='%s' session-type='%s' session-x11-display='%s' session-x11-display-device='%s' session-display-device='%s' session-remote-host-name='%s' session-is-local=%s session-unix-user=%u session-creation-time='%s'",
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
add_log_for_seat_session_removed (GString            *str,
                                  CkEventLoggerEvent *event)
{
        CkEventLoggerSeatSessionRemovedEvent *e;

        e = (CkEventLoggerSeatSessionRemovedEvent *)event;
        g_string_append_printf (str,
                                " seat-id='%s' session-id='%s' session-type='%s' session-x11-display='%s' session-x11-display-device='%s' session-display-device='%s' session-remote-host-name='%s' session-is-local=%s session-unix-user=%u session-creation-time='%s'",
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
add_log_for_seat_active_session_changed (GString            *str,
                                         CkEventLoggerEvent *event)
{
        CkEventLoggerSeatActiveSessionChangedEvent *e;

        e = (CkEventLoggerSeatActiveSessionChangedEvent *)event;
        g_string_append_printf (str,
                                " seat-id='%s' session-id='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->session_id ? e->session_id : "");
}

static void
add_log_for_seat_device_added (GString            *str,
                               CkEventLoggerEvent *event)
{
        CkEventLoggerSeatDeviceAddedEvent *e;

        e = (CkEventLoggerSeatDeviceAddedEvent *)event;
        g_string_append_printf (str,
                                " seat-id='%s' device-id='%s' device-type='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->device_id ? e->device_id : "",
                                e->device_type ? e->device_type : "");
}

static void
add_log_for_seat_device_removed (GString            *str,
                                 CkEventLoggerEvent *event)
{
        CkEventLoggerSeatDeviceRemovedEvent *e;

        e = (CkEventLoggerSeatDeviceRemovedEvent *)event;
        g_string_append_printf (str,
                                " seat-id='%s' device-id='%s' device-type='%s'",
                                e->seat_id ? e->seat_id : "",
                                e->device_id ? e->device_id : "",
                                e->device_type ? e->device_type : "");
}

static const char *
event_type_to_name (int event_type)
{
        const char *str;
        switch (event_type) {
        case CK_EVENT_LOGGER_EVENT_SEAT_ADDED:
                str = "SEAT_ADDED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_REMOVED:
                str = "SEAT_REMOVED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_ADDED:
                str = "SEAT_SESSION_ADDED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_REMOVED:
                str = "SEAT_SESSION_REMOVED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_ADDED:
                str = "SEAT_DEVICE_ADDED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_REMOVED:
                str = "SEAT_DEVICE_REMOVED";
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                str = "SEAT_ACTIVE_SESSION_CHANGED";
                break;
        default:
                str = "UNKNOWN";
                break;
        }
        return str;
}

static void
add_log_for_any (GString            *str,
                 CkEventLoggerEvent *event)
{
        char *tstr;

        tstr = g_time_val_to_iso8601 (&event->timestamp);

        g_string_append_printf (str,
                                "%s type=%s",
                                tstr,
                                event_type_to_name (event->type));
        g_free (tstr);
}

static gboolean
write_log_for_event (CkEventLogger      *event_logger,
                     CkEventLoggerEvent *event)
{
        GString *str;

        str = g_string_new (NULL);

        add_log_for_any (str, event);

        switch (event->type) {
        case CK_EVENT_LOGGER_EVENT_SEAT_ADDED:
                add_log_for_seat_added (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_REMOVED:
                add_log_for_seat_removed (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_ADDED:
                add_log_for_seat_session_added (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_SESSION_REMOVED:
                add_log_for_seat_session_removed (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_ADDED:
                add_log_for_seat_device_added (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_DEVICE_REMOVED:
                add_log_for_seat_device_removed (str, event);
                break;
        case CK_EVENT_LOGGER_EVENT_SEAT_ACTIVE_SESSION_CHANGED:
                add_log_for_seat_active_session_changed (str, event);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        g_debug ("Writing log for event: %s", str->str);

        if (event_logger->priv->file != NULL) {
                int rc;

                rc = fprintf (event_logger->priv->file, "%s\n", str->str);
                if (rc < 0) {
                        g_warning ("Record was not written to disk (%s)",
                                   g_strerror (errno));
                }
        }

        g_string_free (str, TRUE);

        return TRUE;
}

static void *
writer_thread_start (CkEventLogger *event_logger)
{
        CkEventLoggerEvent *event;

        while ((event = g_async_queue_pop (event_logger->priv->event_queue)) != NULL) {
                write_log_for_event (event_logger, event);
                ck_event_logger_event_free (event);
        }

        g_thread_exit (NULL);
        return NULL;
}

static void
create_writer_thread (CkEventLogger *event_logger)
{
        GError *error;

        g_debug ("Creating thread for log writing");

        error = NULL;
        event_logger->priv->writer_thread = g_thread_create_full ((GThreadFunc)writer_thread_start,
                                                                  event_logger,
                                                                  65536,
                                                                  FALSE,
                                                                  TRUE,
                                                                  G_THREAD_PRIORITY_NORMAL,
                                                                  &error);
        if (event_logger->priv->writer_thread == NULL) {
                g_debug ("Unable to create thread: %s", error->message);
                g_error_free (error);
        }
}

static GObject *
ck_event_logger_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        CkEventLogger      *event_logger;
        CkEventLoggerClass *klass;

        klass = CK_EVENT_LOGGER_CLASS (g_type_class_peek (CK_TYPE_EVENT_LOGGER));

        event_logger = CK_EVENT_LOGGER (G_OBJECT_CLASS (ck_event_logger_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

        if (open_log_file (event_logger)) {
                create_writer_thread (event_logger);
        }

        return G_OBJECT (event_logger);
}

static void
_ck_event_logger_set_log_filename (CkEventLogger  *event_logger,
                                   const char     *filename)
{
        g_free (event_logger->priv->log_filename);
        event_logger->priv->log_filename = g_strdup (filename);
}

static void
ck_event_logger_set_property (GObject            *object,
                              guint               prop_id,
                              const GValue       *value,
                              GParamSpec         *pspec)
{
        CkEventLogger *self;

        self = CK_EVENT_LOGGER (object);

        switch (prop_id) {
        case PROP_LOG_FILENAME:
                _ck_event_logger_set_log_filename (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_event_logger_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        CkEventLogger *self;

        self = CK_EVENT_LOGGER (object);

        switch (prop_id) {
        case PROP_LOG_FILENAME:
                g_value_set_string (value, self->priv->log_filename);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_event_logger_class_init (CkEventLoggerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_event_logger_finalize;
        object_class->constructor = ck_event_logger_constructor;
        object_class->get_property = ck_event_logger_get_property;
        object_class->set_property = ck_event_logger_set_property;

        g_object_class_install_property (object_class,
                                         PROP_LOG_FILENAME,
                                         g_param_spec_string ("log-filename",
                                                              "log-filename",
                                                              "log-filename",
                                                              DEFAULT_LOG_FILENAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_type_class_add_private (klass, sizeof (CkEventLoggerPrivate));
}

static void
ck_event_logger_init (CkEventLogger *event_logger)
{
        event_logger->priv = CK_EVENT_LOGGER_GET_PRIVATE (event_logger);

        event_logger->priv->event_queue = g_async_queue_new ();
}

static void
ck_event_logger_finalize (GObject *object)
{
        CkEventLogger *event_logger;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_EVENT_LOGGER (object));

        event_logger = CK_EVENT_LOGGER (object);

        g_return_if_fail (event_logger->priv != NULL);

        if (event_logger->priv->event_queue != NULL) {
                g_async_queue_unref (event_logger->priv->event_queue);
        }

        if (event_logger->priv->fd != -1) {
                close (event_logger->priv->fd);
        }

        g_free (event_logger->priv->log_filename);

        G_OBJECT_CLASS (ck_event_logger_parent_class)->finalize (object);
}

CkEventLogger *
ck_event_logger_new (const char *filename)
{
        GObject *object;

        object = g_object_new (CK_TYPE_EVENT_LOGGER,
                               "log-filename", filename,
                               NULL);

        return CK_EVENT_LOGGER (object);
}
