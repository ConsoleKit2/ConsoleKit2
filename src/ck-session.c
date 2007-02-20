/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-session.h"
#include "ck-session-glue.h"
#include "ck-marshal.h"
#include "ck-debug.h"

#define CK_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_SESSION, CkSessionPrivate))

#define CK_DBUS_PATH "/org/freedesktop/ConsoleKit"
#define CK_DBUS_NAME "org.freedesktop.ConsoleKit"

struct CkSessionPrivate
{
        char            *id;
        char            *cookie;
        char            *seat_id;

        char            *session_type;
        char            *display_device;
        char            *xdisplay;
        char            *host_name;
        guint            uid;

        gboolean         active;
        gboolean         is_local;

        GTimeVal         creation_time;

        gboolean         idle_hint;
        GTimeVal         idle_since_hint;

        DBusGConnection *connection;
        DBusGProxy      *bus_proxy;
};

enum {
        ACTIVATE,
        LOCK,
        UNLOCK,
        ACTIVE_CHANGED,
        IDLE_HINT_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_ID,
        PROP_COOKIE,
        PROP_USER,
        PROP_X11_DISPLAY,
        PROP_DISPLAY_DEVICE,
        PROP_SESSION_TYPE,
        PROP_HOST_NAME,
        PROP_IS_LOCAL,
        PROP_ACTIVE,
        PROP_IDLE_HINT,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_session_class_init  (CkSessionClass *klass);
static void     ck_session_init        (CkSession      *session);
static void     ck_session_finalize    (GObject        *object);

G_DEFINE_TYPE (CkSession, ck_session, G_TYPE_OBJECT)

GQuark
ck_session_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_session_error");
        }

        return ret;
}

static gboolean
register_session (CkSession *session)
{
        GError *error = NULL;

        error = NULL;
        session->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (session->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        session->priv->bus_proxy = dbus_g_proxy_new_for_name (session->priv->connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);

        dbus_g_connection_register_g_object (session->priv->connection, session->priv->id, G_OBJECT (session));

        return TRUE;
}


/*
  lock and unlock are separate functions because:
   1. we don't maintain state for locked
   2. so security policy can be handled separately
*/
gboolean
ck_session_lock (CkSession             *session,
                 DBusGMethodInvocation *context)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        ck_debug ("Emitting lock for session %s", session->priv->id);
        g_signal_emit (session, signals [LOCK], 0);

        dbus_g_method_return (context);

        return TRUE;
}

gboolean
ck_session_unlock (CkSession             *session,
                   DBusGMethodInvocation *context)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        ck_debug ("Emitting unlock for session %s", session->priv->id);
        g_signal_emit (session, signals [UNLOCK], 0);

        dbus_g_method_return (context);

        return TRUE;
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (CkSession   *session,
                 const char  *sender,
                 uid_t       *calling_uid,
                 pid_t       *calling_pid)
{
        gboolean res;
        GError  *error = NULL;

        res = FALSE;

        if (sender == NULL) {
                goto out;
        }

        if (! dbus_g_proxy_call (session->priv->bus_proxy, "GetConnectionUnixUser", &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_uid,
                                 G_TYPE_INVALID)) {
                g_warning ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        if (! dbus_g_proxy_call (session->priv->bus_proxy, "GetConnectionUnixProcessID", &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_pid,
                                 G_TYPE_INVALID)) {
                g_warning ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        res = TRUE;

        ck_debug ("uid = %d", *calling_uid);
        ck_debug ("pid = %d", *calling_pid);

out:
        return res;
}

static gboolean
session_set_idle_hint_internal (CkSession      *session,
                                gboolean        idle_hint)
{
        if (session->priv->idle_hint != idle_hint) {
                session->priv->idle_hint = idle_hint;
                g_object_notify (G_OBJECT (session), "idle-hint");

                /* FIXME: can we get a time from the dbus message? */
                g_get_current_time (&session->priv->idle_since_hint);

                ck_debug ("Emitting idle-changed for session %s", session->priv->id);
                g_signal_emit (session, signals [IDLE_HINT_CHANGED], 0, idle_hint);
        }

        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Session1 \
  org.freedesktop.ConsoleKit.Session.SetIdleHint boolean:TRUE
*/
gboolean
ck_session_set_idle_hint (CkSession             *session,
                          gboolean               idle_hint,
                          DBusGMethodInvocation *context)
{
        char       *sender;
        uid_t       calling_uid;
        pid_t       calling_pid;
        gboolean    res;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        sender = dbus_g_method_get_sender (context);

        res = get_caller_info (session,
                               sender,
                               &calling_uid,
                               &calling_pid);
        g_free (sender);

        if (! res) {
                GError *error;
                error = g_error_new (CK_SESSION_ERROR,
                                     CK_SESSION_ERROR_GENERAL,
                                     _("Unable to lookup information about calling process '%d'"),
                                     calling_pid);
                g_warning ("stat on pid %d failed", calling_pid);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        /* only restrict this by UID for now */
        if (session->priv->uid != calling_uid) {
                GError *error;
                error = g_error_new (CK_SESSION_ERROR,
                                     CK_SESSION_ERROR_GENERAL,
                                     _("Only session owner may set idle hint state"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        session_set_idle_hint_internal (session, idle_hint);
        dbus_g_method_return (context);

        return TRUE;
}

gboolean
ck_session_get_idle_hint (CkSession *session,
                          gboolean  *idle_hint,
                          GError   **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (idle_hint != NULL) {
                *idle_hint = session->priv->idle_hint;
        }

        return TRUE;
}

gboolean
ck_session_get_idle_since_hint (CkSession *session,
                                char     **iso8601_datetime,
                                GError   **error)
{
        char *date_str;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        date_str = NULL;
        if (session->priv->idle_hint) {
                date_str = g_time_val_to_iso8601 (&session->priv->idle_since_hint);
        }

        if (iso8601_datetime != NULL) {
                *iso8601_datetime = g_strdup (date_str);
        }

        g_free (date_str);

        return TRUE;
}

gboolean
ck_session_activate (CkSession             *session,
                     DBusGMethodInvocation *context)
{
        gboolean res;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        res = FALSE;
        g_signal_emit (session, signals [ACTIVATE], 0, context, &res);
        if (! res) {
                GError *error;

                /* if the signal is not handled then either:
                   a) aren't attached to seat
                   b) seat doesn't support activation changes */
                ck_debug ("Activate signal not handled");

                error = g_error_new (CK_SESSION_ERROR,
                                     CK_SESSION_ERROR_GENERAL,
                                     _("Unable to activate session"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
}

gboolean
ck_session_set_active (CkSession      *session,
                       gboolean        active,
                       GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (session->priv->active != active) {
                session->priv->active = active;
                g_signal_emit (session, signals [ACTIVE_CHANGED], 0, active);
        }

        return TRUE;
}

gboolean
ck_session_set_is_local (CkSession      *session,
                         gboolean        is_local,
                         GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (session->priv->is_local != is_local) {
                session->priv->is_local = is_local;
        }

        return TRUE;
}

gboolean
ck_session_get_id (CkSession      *session,
                   char          **id,
                   GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (id != NULL) {
                *id = g_strdup (session->priv->id);
        }

        return TRUE;
}

gboolean
ck_session_get_seat_id (CkSession      *session,
                        char          **id,
                        GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (id != NULL) {
                *id = g_strdup (session->priv->seat_id);
        }

        return TRUE;
}

gboolean
ck_session_get_user (CkSession      *session,
                     guint          *uid,
                     GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (uid != NULL) {
                *uid = session->priv->uid;
        }

        return TRUE;
}

gboolean
ck_session_get_x11_display (CkSession      *session,
                            char          **xdisplay,
                            GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (xdisplay != NULL) {
                *xdisplay = g_strdup (session->priv->xdisplay);
        }

        return TRUE;
}

gboolean
ck_session_get_display_device (CkSession      *session,
                               char          **display_device,
                               GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (display_device != NULL) {
                *display_device = g_strdup (session->priv->display_device);
        }

        return TRUE;
}

gboolean
ck_session_get_host_name (CkSession      *session,
                          char          **host_name,
                          GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (host_name != NULL) {
                *host_name = g_strdup (session->priv->host_name);
        }

        return TRUE;
}

gboolean
ck_session_get_creation_time (CkSession      *session,
                              char          **iso8601_datetime,
                              GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (iso8601_datetime != NULL) {
                *iso8601_datetime = g_time_val_to_iso8601 (&session->priv->creation_time);
        }

        return TRUE;
}

gboolean
ck_session_get_session_type (CkSession      *session,
                             char          **type,
                             GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (type != NULL) {
                *type = g_strdup (session->priv->session_type);
        }

        return TRUE;
}

gboolean
ck_session_is_active (CkSession      *session,
                      gboolean       *active,
                      GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (active != NULL) {
                *active = session->priv->active;
        }

        return TRUE;
}

gboolean
ck_session_is_local (CkSession      *session,
                     gboolean       *local,
                     GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (local != NULL) {
                *local = session->priv->is_local;
        }

        return TRUE;
}

gboolean
ck_session_set_id (CkSession      *session,
                   const char     *id,
                   GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->id);
        session->priv->id = g_strdup (id);

        return TRUE;
}

gboolean
ck_session_set_cookie (CkSession      *session,
                       const char     *cookie,
                       GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->cookie);
        session->priv->cookie = g_strdup (cookie);

        return TRUE;
}

gboolean
ck_session_set_seat_id (CkSession      *session,
                        const char     *id,
                        GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->seat_id);
        session->priv->seat_id = g_strdup (id);

        return TRUE;
}

gboolean
ck_session_set_user (CkSession      *session,
                     guint           uid,
                     GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        session->priv->uid = uid;

        return TRUE;
}

gboolean
ck_session_set_x11_display (CkSession      *session,
                            const char     *xdisplay,
                            GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->xdisplay);
        session->priv->xdisplay = g_strdup (xdisplay);

        return TRUE;
}

gboolean
ck_session_set_display_device (CkSession      *session,
                               const char     *display_device,
                               GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->display_device);
        session->priv->display_device = g_strdup (display_device);

        return TRUE;
}

gboolean
ck_session_set_host_name (CkSession      *session,
                          const char     *host_name,
                          GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->host_name);
        session->priv->host_name = g_strdup (host_name);

        return TRUE;
}

gboolean
ck_session_set_session_type (CkSession      *session,
                             const char     *type,
                             GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->session_type);
        session->priv->session_type = g_strdup (type);

        return TRUE;
}

static void
ck_session_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
        CkSession *self;

        self = CK_SESSION (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                ck_session_set_active (self, g_value_get_boolean (value), NULL);
                break;
        case PROP_IS_LOCAL:
                ck_session_set_is_local (self, g_value_get_boolean (value), NULL);
                break;
        case PROP_ID:
                ck_session_set_id (self, g_value_get_string (value), NULL);
                break;
        case PROP_COOKIE:
                ck_session_set_cookie (self, g_value_get_string (value), NULL);
                break;
        case PROP_SESSION_TYPE:
                ck_session_set_session_type (self, g_value_get_string (value), NULL);
                break;
        case PROP_X11_DISPLAY:
                ck_session_set_x11_display (self, g_value_get_string (value), NULL);
                break;
        case PROP_DISPLAY_DEVICE:
                ck_session_set_display_device (self, g_value_get_string (value), NULL);
                break;
        case PROP_USER:
                ck_session_set_user (self, g_value_get_uint (value), NULL);
                break;
        case PROP_HOST_NAME:
                ck_session_set_host_name (self, g_value_get_string (value), NULL);
                break;
        case PROP_IDLE_HINT:
                session_set_idle_hint_internal (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_session_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        CkSession *self;

        self = CK_SESSION (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->active);
                break;
        case PROP_IS_LOCAL:
                g_value_set_boolean (value, self->priv->is_local);
                break;
        case PROP_ID:
                g_value_set_string (value, self->priv->id);
                break;
        case PROP_COOKIE:
                g_value_set_string (value, self->priv->cookie);
                break;
        case PROP_SESSION_TYPE:
                g_value_set_string (value, self->priv->session_type);
                break;
        case PROP_X11_DISPLAY:
                g_value_set_string (value, self->priv->xdisplay);
                break;
        case PROP_DISPLAY_DEVICE:
                g_value_set_string (value, self->priv->display_device);
                break;
        case PROP_USER:
                g_value_set_uint (value, self->priv->uid);
                break;
        case PROP_HOST_NAME:
                g_value_set_string (value, self->priv->host_name);
                break;
        case PROP_IDLE_HINT:
                g_value_set_boolean (value, self->priv->idle_hint);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_session_class_init (CkSessionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = ck_session_get_property;
        object_class->set_property = ck_session_set_property;
        object_class->finalize = ck_session_finalize;

        signals [ACTIVE_CHANGED] =
                g_signal_new ("active-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, active_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);
        signals [ACTIVATE] =
                g_signal_new ("activate",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, activate),
                              NULL,
                              NULL,
                              ck_marshal_BOOLEAN__POINTER,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_POINTER);
        signals [LOCK] =
                g_signal_new ("lock",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, lock),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [UNLOCK] =
                g_signal_new ("unlock",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, unlock),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [IDLE_HINT_CHANGED] =
                g_signal_new ("idle-hint-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, idle_hint_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("active",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_IS_LOCAL,
                                         g_param_spec_boolean ("is-local",
                                                               NULL,
                                                               NULL,
                                                               TRUE,
                                                               G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_COOKIE,
                                         g_param_spec_string ("cookie",
                                                              "cookie",
                                                              "cookie",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_TYPE,
                                         g_param_spec_string ("session-type",
                                                              "session-type",
                                                              "session type",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_X11_DISPLAY,
                                         g_param_spec_string ("x11-display",
                                                              "x11-display",
                                                              "X11 Display",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_DEVICE,
                                         g_param_spec_string ("display-device",
                                                              "display-device",
                                                              "Display device",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_HOST_NAME,
                                         g_param_spec_string ("host-name",
                                                              "host-name",
                                                              "Host name",
                                                              NULL,
                                                              G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_USER,
                                         g_param_spec_uint ("user",
                                                            "User Id",
                                                            "User Id",
                                                            0,
                                                            G_MAXINT,
                                                            0,
                                                            G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_ACTIVE,
                                         g_param_spec_boolean ("idle-hint",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (CkSessionPrivate));

        dbus_g_object_type_install_info (CK_TYPE_SESSION, &dbus_glib_ck_session_object_info);
}

static void
ck_session_init (CkSession *session)
{
        session->priv = CK_SESSION_GET_PRIVATE (session);

        /* FIXME: should we have a property for this? */
        g_get_current_time (&session->priv->creation_time);
}

static void
ck_session_finalize (GObject *object)
{
        CkSession *session;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_SESSION (object));

        session = CK_SESSION (object);

        g_return_if_fail (session->priv != NULL);

        g_free (session->priv->id);
        g_free (session->priv->cookie);
        g_free (session->priv->seat_id);
        g_free (session->priv->session_type);
        g_free (session->priv->xdisplay);
        g_free (session->priv->host_name);

        G_OBJECT_CLASS (ck_session_parent_class)->finalize (object);
}

CkSession *
ck_session_new (const char *ssid,
                const char *cookie)
{
        GObject *object;
        gboolean res;

        object = g_object_new (CK_TYPE_SESSION,
                               "id", ssid,
                               "cookie", cookie,
                               NULL);
        res = register_session (CK_SESSION (object));
        if (! res) {
                g_object_unref (object);
                return NULL;
        }

        return CK_SESSION (object);
}

#define CK_TYPE_PARAMETER_STRUCT (dbus_g_type_get_struct ("GValueArray", \
                                                          G_TYPE_STRING,  \
                                                          G_TYPE_VALUE, \
                                                          G_TYPE_INVALID))

CkSession *
ck_session_new_with_parameters (const char      *ssid,
                                const char      *cookie,
                                const GPtrArray *parameters)
{
        GObject *object;
        gboolean res;
        int      i;

        object = g_object_new (CK_TYPE_SESSION,
                               "id", ssid,
                               "cookie", cookie,
                               NULL);

        for (i = 0; i < parameters->len; i++) {
                GValue      val_struct = { 0, };
                const char *prop_name;
                GValue     *prop_val;

                g_value_init (&val_struct, CK_TYPE_PARAMETER_STRUCT);
                g_value_set_static_boxed (&val_struct, g_ptr_array_index (parameters, i));

                dbus_g_type_struct_get (&val_struct,
                                        0, &prop_name,
                                        1, &prop_val,
                                        G_MAXUINT);

                g_object_set_property (object, prop_name, prop_val);
                g_value_unset (prop_val);
        }

        res = register_session (CK_SESSION (object));
        if (! res) {
                g_object_unref (object);
                return NULL;
        }

        return CK_SESSION (object);
}
