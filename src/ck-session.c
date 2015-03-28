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
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "ck-tty-idle-monitor.h"
#include "ck-session.h"
#include "ck-marshal.h"
#include "ck-run-programs.h"

#define CK_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_SESSION, CkSessionPrivate))

#define CK_SESSION_DBUS_NAME "org.freedesktop.ConsoleKit.Session"

#define NONULL_STRING(x) ((x) != NULL ? (x) : "")

#define IDLE_TIME_SECS 60

struct CkSessionPrivate
{
        char            *id;
        char            *cookie;
        char            *seat_id;

        char            *login_session_id;
        guint            uid;

        GTimeVal         creation_time;

        CkTtyIdleMonitor *idle_monitor;

        GTimeVal         idle_since_hint;

        GDBusConnection *connection;
        GDBusProxy      *bus_proxy;
};

/* Private properties not exported over D-BUS */
enum {
        PROP_0,
        PROP_ID,
        PROP_COOKIE,
        PROP_LOGIN_SESSION_ID,
};


static void     ck_session_class_init  (CkSessionClass         *klass);
static void     ck_session_init        (CkSession              *session);
static void     ck_session_iface_init  (ConsoleKitSessionIface *iface);
static void     ck_session_finalize    (GObject                *object);


G_DEFINE_TYPE_WITH_CODE (CkSession, ck_session, CONSOLE_KIT_TYPE_SESSION_SKELETON, G_IMPLEMENT_INTERFACE (CONSOLE_KIT_TYPE_SESSION, ck_session_iface_init));

static const GDBusErrorEntry ck_session_error_entries[] =
{
        { CK_SESSION_ERROR_FAILED,                  CK_SESSION_DBUS_NAME ".Error.Failed" },
        { CK_SESSION_ERROR_GENERAL,                 CK_SESSION_DBUS_NAME ".Error.General" },
        { CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, CK_SESSION_DBUS_NAME ".Error.InsufficientPermission" },
        { CK_SESSION_ERROR_NOT_SUPPORTED,           CK_SESSION_DBUS_NAME ".Error.NotSupported" }
};

GQuark
ck_session_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("ck_session_error",
                                            &quark_volatile,
                                            ck_session_error_entries,
                                            G_N_ELEMENTS (ck_session_error_entries));

        return (GQuark) quark_volatile;
}
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
ck_session_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0)
    {
      static const GEnumValue values[] =
        {
          ENUM_ENTRY (CK_SESSION_ERROR_FAILED,                  "Failed"),
          ENUM_ENTRY (CK_SESSION_ERROR_GENERAL,                 "General"),
          ENUM_ENTRY (CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, "InsufficientPermission"),
          ENUM_ENTRY (CK_SESSION_ERROR_NOT_SUPPORTED,           "NotSupported"),
          { 0, 0, 0 }
        };
      g_assert (CK_SESSION_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
      etype = g_enum_register_static ("Error", values);
    }
  return etype;
}

static void
throw_error (GDBusMethodInvocation *context,
             gint                   error_code,
             const gchar           *format,
             ...)
{
        va_list args;
        gchar *message;

        va_start (args, format);
        message = g_strdup_vprintf (format, args);
        va_end (args);

        g_dbus_method_invocation_return_error (context, CK_SESSION_ERROR, error_code, "%s", message);

        g_free (message);
}

static gboolean
register_session (CkSession *session, GDBusConnection *connection)
{
        GError   *error = NULL;

        g_debug ("register session");

        error = NULL;

        if (connection == NULL) {
                session->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        } else {
                session->priv->connection = connection;
        }


        if (session->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        g_debug ("exporting path %s", session->priv->id);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_SESSION (session)),
                                               session->priv->connection,
                                               session->priv->id,
                                               &error)) {
                if (error != NULL) {
                        g_critical ("error exporting interface: %s", error->message);
                        g_error_free (error);
                        return FALSE;
                }
        }

        g_debug ("exported on %s", g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_SESSION (session))));

        /* connect to DBus for get_caller_info */
        session->priv->bus_proxy = g_dbus_proxy_new_sync (session->priv->connection,
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                          NULL,
                                                          "org.freedesktop.DBus",
                                                          "/org/freedesktop/DBus/Bus",
                                                          "org.freedesktop.DBus",
                                                          NULL,
                                                          &error);
        if (session->priv->bus_proxy == NULL) {
                g_warning ("cannot connect to DBus: %s", error->message);
                g_error_free (error);
        }

        return TRUE;
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (CkSession   *session,
                 const char  *sender,
                 uid_t       *calling_uid,
                 pid_t       *calling_pid)
{
        gboolean  res   = FALSE;
        GVariant *value = NULL;
        GError   *error = NULL;

        if (sender == NULL) {
                goto out;
        }

        value = g_dbus_proxy_call_sync (session->priv->bus_proxy, "GetConnectionUnixUser",
                                        g_variant_new ("(s)", sender),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        2000,
                                        NULL,
                                        &error);

        if (value == NULL) {
                g_warning ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }
        g_variant_get (value, "(u)", &calling_uid);

        value = g_dbus_proxy_call_sync (session->priv->bus_proxy, "GetConnectionUnixProcessID",
                                        g_variant_new ("(s)", sender),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        2000,
                                        NULL,
                                        &error);

        if (value == NULL) {
                g_warning ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }
        g_variant_get (value, "(u)", &calling_pid);

        res = TRUE;

        g_debug ("uid = %d", *calling_uid);
        g_debug ("pid = %d", *calling_pid);

out:
        return res;
}

static gboolean
session_set_idle_hint_internal (CkSession      *session,
                                gboolean        idle_hint)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);

        if (console_kit_session_get_idle_hint (cksession) != idle_hint) {
                console_kit_session_set_idle_hint (cksession, idle_hint);

                /* FIXME: can we get a time from the dbus message? */
                g_get_current_time (&session->priv->idle_since_hint);

                g_debug ("Emitting idle-changed for session %s", session->priv->id);
                console_kit_session_emit_idle_hint_changed (cksession, idle_hint);
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
static gboolean
dbus_set_idle_hint (ConsoleKitSession     *cksession,
                    GDBusMethodInvocation *context,
                    gboolean               idle_hint)
{
        const char *sender;
        uid_t       calling_uid;
        pid_t       calling_pid;
        gboolean    res;
        CkSession *session;

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        session = CK_SESSION(cksession);

        sender = g_dbus_method_invocation_get_sender (context);

        res = get_caller_info (session,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                g_warning ("stat on pid %d failed", calling_pid);
                throw_error (context, CK_SESSION_ERROR_FAILED, _("Unable to lookup information about calling process '%d'"), calling_pid);
                return FALSE;
        }

        /* only restrict this by UID for now */
        if (session->priv->uid != calling_uid) {
                throw_error (context, CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, _("Only session owner may set idle hint state"));
                return FALSE;
        }

        session_set_idle_hint_internal (session, idle_hint);

        console_kit_session_complete_set_idle_hint (cksession, context);
        return TRUE;
}

gboolean
ck_session_set_active (CkSession      *session,
                       gboolean        active,
                       GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        if (console_kit_session_get_active (cksession) != active) {
                console_kit_session_set_active (cksession, active);
                console_kit_session_emit_active_changed (cksession, active);
        }

        return TRUE;
}

static gboolean
dbus_activate (ConsoleKitSession     *cksession,
               GDBusMethodInvocation *context)
{
        ck_session_set_active (CK_SESSION (cksession), TRUE, NULL);

        console_kit_session_complete_activate (cksession, context);
        return TRUE;
}

gboolean
ck_session_get_id (CkSession *session,
                   char **id,
                   GError **error)
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
ck_session_set_unix_user (CkSession      *session,
                          guint           uid,
                          GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_unix_user (CONSOLE_KIT_SESSION (session), uid);

        return TRUE;
}

gboolean
ck_session_set_x11_display (CkSession      *session,
                            const char     *x11_display,
                            GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_x11_display (CONSOLE_KIT_SESSION (session), x11_display);

        return TRUE;
}

gboolean
ck_session_set_display_device (CkSession      *session,
                               const char     *display_device,
                               GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_display_device (cksession, display_device);

        return TRUE;
}

gboolean
ck_session_set_x11_display_device (CkSession      *session,
                                   const char     *x11_display_device,
                                   GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_x11_display_device (cksession, x11_display_device);

        return TRUE;
}

gboolean
ck_session_set_remote_host_name (CkSession      *session,
                                 const char     *remote_host_name,
                                 GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_remote_host_name (cksession, remote_host_name);

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
ck_session_set_is_local (CkSession      *session,
                         gboolean        is_local,
                         GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_is_local (CONSOLE_KIT_SESSION (session), is_local);

        return TRUE;
}

gboolean
ck_session_is_local (CkSession *session,
                     gboolean *local,
                     GError **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (local != NULL) {
                *local = console_kit_session_get_is_local (CONSOLE_KIT_SESSION (session));
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
ck_session_set_login_session_id (CkSession      *session,
                                 const char     *login_session_id,
                                 GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->login_session_id);
        session->priv->login_session_id = g_strdup (login_session_id);

        return TRUE;
}

gboolean
ck_session_set_session_type (CkSession      *session,
                             const char     *type,
                             GError        **error)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_session_type (cksession, type);

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
        case PROP_ID:
                ck_session_set_id (self, g_value_get_string (value), NULL);
                break;
        case PROP_COOKIE:
                ck_session_set_cookie (self, g_value_get_string (value), NULL);
                break;
        case PROP_LOGIN_SESSION_ID:
                ck_session_set_login_session_id (self, g_value_get_string (value), NULL);
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
        case PROP_ID:
                g_value_set_string (value, self->priv->id);
                break;
        case PROP_COOKIE:
                g_value_set_string (value, self->priv->cookie);
                break;
        case PROP_LOGIN_SESSION_ID:
                g_value_set_string (value, self->priv->login_session_id);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')
static gboolean
session_is_text (CkSession *session)
{
        ConsoleKitSession *cksession;
        gboolean ret;

        ret = FALSE;
        cksession = CONSOLE_KIT_SESSION (session);

        if (! IS_STR_SET (console_kit_session_get_x11_display_device (cksession))
            && ! IS_STR_SET (console_kit_session_get_x11_display (cksession))
            && IS_STR_SET (console_kit_session_get_display_device (cksession))) {
                ret = TRUE;
        }

        g_debug ("Identified session '%s' as %s",
                  session->priv->id,
                  ret ? "text" : "graphical");

        return ret;
}

static void
tty_idle_changed_cb (CkTtyIdleMonitor *monitor,
                     gboolean          idle_hint,
                     CkSession        *session)
{
        session_set_idle_hint_internal (session, idle_hint);
}

static void
session_add_activity_watch (CkSession *session)
{
        if (session->priv->idle_monitor == NULL) {
                session->priv->idle_monitor = ck_tty_idle_monitor_new (console_kit_session_get_display_device (CONSOLE_KIT_SESSION (session)));
                g_signal_connect (session->priv->idle_monitor,
                                  "idle-hint-changed",
                                  G_CALLBACK (tty_idle_changed_cb),
                                  session);

        }
        ck_tty_idle_monitor_start (session->priv->idle_monitor);
}

static void
session_remove_activity_watch (CkSession *session)
{
        if (session->priv->idle_monitor == NULL) {
                return;
        }

        ck_tty_idle_monitor_stop (session->priv->idle_monitor);
        g_object_unref (session->priv->idle_monitor);
        session->priv->idle_monitor = NULL;
}

static GObject *
ck_session_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        CkSession      *session;

        session = CK_SESSION (G_OBJECT_CLASS (ck_session_parent_class)->constructor (type,
                                                                                     n_construct_properties,
                                                                                     construct_properties));
        if (session_is_text (session)) {
                session_add_activity_watch (session);
        }

        return G_OBJECT (session);
}

static void
ck_session_class_init (CkSessionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = ck_session_constructor;
        object_class->get_property = ck_session_get_property;
        object_class->set_property = ck_session_set_property;
        object_class->finalize = ck_session_finalize;


        /* Install private properties we're not exporting over D-BUS */
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
                                         PROP_LOGIN_SESSION_ID,
                                         g_param_spec_string ("login-session-id",
                                                              "login-session-id",
                                                              "login session id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CkSessionPrivate));
}

static void
ck_session_init (CkSession *session)
{
        session->priv = CK_SESSION_GET_PRIVATE (session);

        /* FIXME: should we have a property for this? */
        g_get_current_time (&session->priv->creation_time);
}

static void
ck_session_iface_init (ConsoleKitSessionIface *iface)
{
        iface->handle_activate      = dbus_activate;
        iface->handle_set_idle_hint = dbus_set_idle_hint;
}

static void
ck_session_finalize (GObject *object)
{
        CkSession *session;

        g_debug ("ck_session_finalize");

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_SESSION (object));

        session = CK_SESSION (object);

        g_return_if_fail (session->priv != NULL);

        session_remove_activity_watch (session);

        g_free (session->priv->id);
        g_free (session->priv->cookie);
        g_free (session->priv->login_session_id);

        G_OBJECT_CLASS (ck_session_parent_class)->finalize (object);
}

CkSession *
ck_session_new (const char      *ssid,
                const char      *cookie,
                GDBusConnection *connection)
{
        GObject *object;
        gboolean res;

        object = g_object_new (CK_TYPE_SESSION,
                               "id", ssid,
                               "cookie", cookie,
                               NULL);
        res = register_session (CK_SESSION (object), connection);
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
                                const GPtrArray *parameters,
                                GDBusConnection *connection)
{
        GObject      *object;
        gboolean      res;
        int           i;
        GParameter   *params;
        guint         n_allocated_params;
        guint         n_params;
        GObjectClass *class;
        GType         object_type;

        object_type = CK_TYPE_SESSION;
        class = g_type_class_ref (object_type);

        n_allocated_params = 2;
        if (parameters != NULL) {
                n_allocated_params += parameters->len;
        }

        params = g_new0 (GParameter, n_allocated_params);

        n_params = 0;
        params[n_params].name = g_strdup ("id");
        params[n_params].value.g_type = 0;
        g_value_init (&params[n_params].value, G_TYPE_STRING);
        g_value_set_string (&params[n_params].value, ssid);
        n_params++;

        params[n_params].name = g_strdup ("cookie");
        params[n_params].value.g_type = 0;
        g_value_init (&params[n_params].value, G_TYPE_STRING);
        g_value_set_string (&params[n_params].value, cookie);
        n_params++;

        if (parameters != NULL) {
                for (i = 0; i < parameters->len; i++) {
                        gboolean    res;
                        GValue      val_struct = { 0, };
                        char       *prop_name;
                        GValue     *prop_val;
                        GParamSpec *pspec;

                        g_value_init (&val_struct, CK_TYPE_PARAMETER_STRUCT);
                        g_value_set_static_boxed (&val_struct, g_ptr_array_index (parameters, i));

                        res = dbus_g_type_struct_get (&val_struct,
                                                      0, &prop_name,
                                                      1, &prop_val,
                                                      G_MAXUINT);
                        if (! res) {
                                g_debug ("Unable to extract parameter input");
                                goto cont;
                        }

                        if (prop_name == NULL) {
                                g_debug ("Skipping NULL parameter");
                                goto cont;
                        }

                        if (strcmp (prop_name, "id") == 0
                            || strcmp (prop_name, "cookie") == 0) {
                                g_debug ("Skipping restricted parameter: %s", prop_name);
                                goto cont;
                        }

                        pspec = g_object_class_find_property (class, prop_name);
                        if (! pspec) {
                                g_debug ("Skipping unknown parameter: %s", prop_name);
                                goto cont;
                        }

                        if (!(pspec->flags & G_PARAM_WRITABLE)) {
                                g_debug ("property '%s' is not writable", pspec->name);
                                goto cont;
                        }

                        params[n_params].name = g_strdup (prop_name);
                        params[n_params].value.g_type = 0;
                        g_value_init (&params[n_params].value, G_PARAM_SPEC_VALUE_TYPE (pspec));
                        res = g_value_transform (prop_val, &params[n_params].value);
                        if (! res) {
                                g_debug ("unable to transform property value for '%s'", pspec->name);
                                goto cont;
                        }

                        n_params++;
                cont:
                        g_free (prop_name);
                        if (prop_val != NULL) {
                                g_value_unset (prop_val);
                                g_free (prop_val);
                        }
                }
        }

        object = g_object_newv (object_type, n_params, params);

        while (n_params--) {
                g_free ((char *)params[n_params].name);
                g_value_unset (&params[n_params].value);
        }
        g_free (params);
        g_type_class_unref (class);

        res = register_session (CK_SESSION (object), connection);
        if (! res) {
                g_object_unref (object);
                return NULL;
        }

        return CK_SESSION (object);
}

void
ck_session_run_programs (CkSession  *session,
                         const char *action)
{
        ConsoleKitSession *cksession;
        int   n;
        char *extra_env[11]; /* be sure to adjust this as needed */

        n = 0;
        cksession = CONSOLE_KIT_SESSION (session);

        extra_env[n++] = g_strdup_printf ("CK_SESSION_ID=%s", session->priv->id);
        if (console_kit_session_get_session_type (cksession) != NULL) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_TYPE=%s", console_kit_session_get_session_type (cksession));
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_SEAT_ID=%s", session->priv->seat_id);
        extra_env[n++] = g_strdup_printf ("CK_SESSION_USER_UID=%d", session->priv->uid);
        if (console_kit_session_get_display_device (cksession) != NULL && strlen (console_kit_session_get_display_device (cksession)) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_DISPLAY_DEVICE=%s", console_kit_session_get_display_device (cksession));
        }
        if (console_kit_session_get_x11_display_device (cksession) != NULL && strlen (console_kit_session_get_x11_display_device (cksession) ) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_X11_DISPLAY_DEVICE=%s", console_kit_session_get_x11_display_device (cksession) );
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_X11_DISPLAY=%s",
                console_kit_session_get_x11_display (cksession)  ? console_kit_session_get_x11_display (cksession)  : "");
        if (console_kit_session_get_remote_host_name (cksession)  != NULL && strlen (console_kit_session_get_remote_host_name (cksession)) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_REMOTE_HOST_NAME=%s", console_kit_session_get_remote_host_name (cksession));
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_IS_ACTIVE=%s", console_kit_session_get_active (cksession) ? "true" : "false");
        extra_env[n++] = g_strdup_printf ("CK_SESSION_IS_LOCAL=%s", console_kit_session_get_is_local (cksession) ? "true" : "false");
        extra_env[n++] = NULL;

        g_assert(n <= G_N_ELEMENTS(extra_env));

        ck_run_programs (SYSCONFDIR "/ConsoleKit/run-session.d", action, extra_env);
        ck_run_programs (PREFIX "/lib/ConsoleKit/run-session.d", action, extra_env);

        for (n = 0; extra_env[n] != NULL; n++) {
                g_free (extra_env[n]);
        }
}

void
ck_session_dump (CkSession *session,
                 GKeyFile  *key_file)
{
        char *s;
        char *group_name;
        ConsoleKitSession *cksession;

        cksession = CONSOLE_KIT_SESSION (session);

        group_name = g_strdup_printf ("Session %s", session->priv->id);
        g_key_file_set_integer (key_file, group_name, "uid", session->priv->uid);
        g_key_file_set_string (key_file,
                               group_name,
                               "seat",
                               NONULL_STRING (session->priv->seat_id));
        if (console_kit_session_get_session_type (cksession) != NULL) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "type",
                                       NONULL_STRING (console_kit_session_get_session_type (cksession)));
        }
        if (session->priv->login_session_id != NULL && strlen (session->priv->login_session_id) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "login_session_id",
                                       NONULL_STRING (session->priv->login_session_id));
        }
        if (console_kit_session_get_display_device (cksession) != NULL && strlen (console_kit_session_get_display_device (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "display_device",
                                       NONULL_STRING (console_kit_session_get_display_device (cksession)));
        }
        if (console_kit_session_get_x11_display_device (cksession) != NULL && strlen (console_kit_session_get_x11_display_device (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "x11_display_device",
                                       NONULL_STRING (console_kit_session_get_x11_display_device (cksession)));
        }
        if (console_kit_session_get_x11_display (cksession) != NULL && strlen (console_kit_session_get_x11_display (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "x11_display",
                                       NONULL_STRING (console_kit_session_get_x11_display (cksession)));
        }
        if (console_kit_session_get_remote_host_name (cksession) != NULL && strlen (console_kit_session_get_remote_host_name (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "remote_host_name",
                                       NONULL_STRING (console_kit_session_get_remote_host_name (cksession)));
        }
        g_key_file_set_string (key_file,
                               group_name,
                               "remote_host_name",
                               NONULL_STRING (console_kit_session_get_remote_host_name (cksession)));

        console_kit_session_set_active (cksession, g_key_file_get_boolean (key_file, group_name, "is_active", NULL));
        console_kit_session_set_is_local (cksession, g_key_file_get_boolean (key_file, group_name, "is_local", NULL));

        s = g_time_val_to_iso8601 (&(session->priv->creation_time));
        g_key_file_set_string (key_file,
                               group_name,
                               "creation_time",
                               NONULL_STRING (s));
        g_free (s);

        g_free (group_name);
}
