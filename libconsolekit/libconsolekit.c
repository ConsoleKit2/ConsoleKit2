/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2017, Eric Koegel <eric.koegel@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>


#include "libconsolekit.h"

#define CK_NAME          "org.freedesktop.ConsoleKit"
#define CK_MANAGER_PATH  "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_NAME  CK_NAME ".Manager"
#define CK_SEAT_NAME     CK_NAME ".Seat"
#define CK_SESSION_NAME  CK_NAME ".Session"


#define CONSOLEKIT_ERROR lib_consolekit_error_quark ()

/**
 * SECTION:libconsolekit
 * @short_description: Helper library to query various ConsoleKit properties
 * @include: libconsolekit.h
 *
 * A #LibConsoleKit provides a way to easily query information from the ConsoleKit
 * daemon.
 */


static void     lib_consolekit_finalize    (GObject *object);


/**
 * LibConsoleKit:
 *
 * The LibConsoleKit struct contains only private data.
 * It should only be accessed through the functions described below.
 */
struct _LibConsoleKit
{
        GObject parent_instance;
};


G_DEFINE_TYPE (LibConsoleKit, lib_consolekit, G_TYPE_OBJECT)

/**
 * lib_consolekit_error_quark:
 *
 * Registers an error quark for #LibConsoleKit if necessary.
 *
 * Returns: the error quark used for #LibConsoleKit errors.
 **/
GQuark
lib_consolekit_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0)
                error_quark = g_quark_from_static_string ("libck2-error-quark");

        return error_quark;
}

static void
lib_consolekit_class_init (LibConsoleKitClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = lib_consolekit_finalize;
}


static void
lib_consolekit_init (LibConsoleKit *ck)
{
}


static void
lib_consolekit_finalize (GObject *object)
{
        G_OBJECT_CLASS (lib_consolekit_parent_class)->finalize (object);
}

/**
 * lib_consolekit_new: (skip)
 *
 * Creates and returns a new instance of #LibConsoleKit.
 *
 * Return value: (transfer full): a new instance of #LibConsoleKit.
 *
 * Since: 1.0
 **/
LibConsoleKit*
lib_consolekit_new (void)
{
        GObject *object;

        object = g_object_new (LIB_TYPE_CONSOLEKIT,
                               NULL);

        return LIB_CONSOLEKIT (object);
}

/**
 * lib_consolekit_seat_get_active:
 * @ck      : A #LibConsoleKit
 * @seat    : The seat to query
 * @session : (out) (transfer full): The active session, if any
 * @uid     : (out) (allow-none): The unix user the session belongs to
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the session and uid of the user who has the active session on
 * the seat, if any session is active. Free the session with g_free when
 * done.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_seat_get_active (LibConsoleKit *ck,
                                const gchar *seat,
                                gchar **session,
                                uid_t *uid,
                                GError **error)
{
        GDBusProxy *seat_proxy = NULL;
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (seat == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Seat must not be NULL");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit seat */
        seat_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                    NULL,
                                                    CK_NAME,
                                                    seat,
                                                    CK_SEAT_NAME,
                                                    NULL,
                                                    error);

        /* failed to connect */
        if (seat_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (seat_proxy,
                                          "GetActiveSession",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the seat proxy */
        g_clear_object(&seat_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "o", session);

        g_variant_unref (variant);
        variant = NULL;

        /* Don't care about uid? We're done */
        if (uid == NULL) {
                return TRUE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       *session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetUnixUser",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "u", uid);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_seat_get_sessions:
 * @ck      : A #LibConsoleKit
 * @seat    : The seat to query
 * @sessions: (out) (transfer full) (array zero-terminated=1): The sessions on the seat, if any
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the sessions on the seat. Free the sessions with g_strfreev when done.
 *
 * Return value: The number of sessions returned or -1 on failure
 *
 * Since: 1.0
 **/
gint
lib_consolekit_seat_get_sessions (LibConsoleKit *ck,
                                  const gchar *seat,
                                  gchar ***sessions,
                                  GError **error)
{
        GDBusProxy *seat_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return -1;
        }

        if (seat == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Seat must not be NULL");
                return -1;
        }

        if (sessions == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Sessions must not be NULL");
                return -1;
        }

        /* connect to the ConsoleKit seat */
        seat_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                    NULL,
                                                    CK_NAME,
                                                    seat,
                                                    CK_SEAT_NAME,
                                                    NULL,
                                                    error);

        /* failed to connect */
        if (seat_proxy == NULL) {
                return -1;
        }

        variant = g_dbus_proxy_call_sync (seat_proxy,
                                          "GetSessions",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the seat proxy */
        g_clear_object(&seat_proxy);

        if (variant == NULL) {
                return -1;
        }

        g_variant_get (variant, "(^ao)", sessions);

        g_variant_unref (variant);
        variant = NULL;

        return g_strv_length (*sessions);
}

/**
 * lib_consolekit_seat_can_multi_session:
 * @ck      : A #LibConsoleKit
 * @seat    : The seat to query
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns whether the provided seat is capable of holding multiple sessions
 * at any given time.
 *
 * Return value: TRUE if capable of multiple sessions.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_seat_can_multi_session (LibConsoleKit *ck,
                                       const gchar *seat,
                                       GError **error)
{
        GDBusProxy *seat_proxy = NULL;
        GVariant   *variant = NULL;
        gboolean    can_activate = FALSE;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (seat == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Seat must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit seat */
        seat_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                    NULL,
                                                    CK_NAME,
                                                    seat,
                                                    CK_SEAT_NAME,
                                                    NULL,
                                                    error);

        /* failed to connect */
        if (seat_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (seat_proxy,
                                          "CanActivateSessions",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the seat proxy */
        g_clear_object(&seat_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "b", &can_activate);

        g_variant_unref (variant);
        variant = NULL;

        return can_activate;
}

/**
 * lib_consolekit_session_is_active:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns whether the provided session is active.
 *
 * Return value: TRUE if the session is active.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_is_active (LibConsoleKit *ck,
                                  const gchar *session,
                                  GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;
        gboolean    is_active = FALSE;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "IsActive",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "b", &is_active);

        g_variant_unref (variant);
        variant = NULL;

        return is_active;
}

/**
 * lib_consolekit_session_is_remote:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns whether the provided session is remote.
 *
 * Return value: TRUE if the session is remote, FALSE if local. Defaults to TRUE on error.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_is_remote (LibConsoleKit *ck,
                                  const gchar *session,
                                  GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;
        gboolean    is_local = FALSE;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return TRUE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "IsLocal",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return TRUE;
        }

        g_variant_get_child (variant, 0, "b", &is_local);

        g_variant_unref (variant);
        variant = NULL;

        /* We flip this because we want to know if we're remote */
        return !is_local;
}

/**
 * lib_consolekit_session_get_uid:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @uid     : (out) (transfer full): unix-user who owns the session
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the unix user who owns the session.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_uid (LibConsoleKit *ck,
                                const gchar *session,
                                uid_t *uid,
                                GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (uid == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "uid must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetUnixUser",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "u", uid);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_seat:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @seat    : (out) (transfer full): The seat the session belongs to
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the seat the session belongs to. Free the seat string
 * with g_free when finished.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_seat (LibConsoleKit *ck,
                                 const gchar *session,
                                 gchar **seat,
                                 GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (seat == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Seat must not be NULL");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetSeatId",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "o", seat);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_service:
 * @ck        : A #LibConsoleKit
 * @session   : The session to query
 * @service   : (out) (transfer full): The session's service
 * @error     : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the service of the provided session. Defaults to:
 * "unspecified" - Unknown session service.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.2.4
 **/
gboolean
lib_consolekit_session_get_service (LibConsoleKit *ck,
                                    const gchar *session,
                                    gchar **service,
                                    GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (service == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "service must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetSessionService",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", service);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_type:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @type   : (out) (transfer full): The session's display type
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the display type of the provided session. The following types
 * may be returned:
 * "x11"         - An X11/Xorg based session
 * "wayland"     - A Wayland based session
 * "tty"         - A text console based session
 * "mir"         - A session using the Mir display server
 * "unspecified" - Unknown session type.
 *
 * Note: Additional types may be added in the future. Free the type
 * string with g_free.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_type (LibConsoleKit *ck,
                                 const gchar *session,
                                 gchar **type,
                                 GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (type == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "type must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetSessionType",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", type);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_class:
 * @ck            : A #LibConsoleKit
 * @session       : The session to query
 * @session_class : (out) (transfer full): The session's class
 * @error         : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the class of the provided session. The following classes
 * may be returned:
 * "user"        - A normal user session, the default
 * "greeter"     - Display Manager pseudo session
 * "lock-screen" - Screensaver based session
 * "background"  - A long running background process that requires its own session
 *
 * Note: Additional classes may be added in the future. Free the session_class
 * string with g_free.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_class (LibConsoleKit *ck,
                                  const gchar *session,
                                  gchar **session_class,
                                  GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (session_class == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "session_class must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetSessionClass",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", session_class);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_state:
 * @ck            : A #LibConsoleKit
 * @session       : The session to query
 * @state         : (out) (transfer full): The session's state
 * @error         : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the current state of the provided session. The following states
 * may be returned:
 * "online"      - Session is logged in but not active
 * "active"      - Session is logged in and active
 * "closing"     - Session is in the process of shutting down
 *
 * Note: Additional states may be added in the future. Free the state
 * string with g_free.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_state (LibConsoleKit *ck,
                                  const gchar *session,
                                  gchar **state,
                                  GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (state == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "state must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetSessionState",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", state);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_display:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @display : (out) (transfer full): The X11 display the session owns
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the display the session has, if any. Free the display string
 * with g_free when finished.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_display (LibConsoleKit *ck,
                                    const gchar *session,
                                    gchar **display,
                                    GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (display == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Display must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetX11Display",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", display);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_remote_host:
 * @ck          : A #LibConsoleKit
 * @session     : The session to query
 * @remote_host : (out) (transfer full): The remote hostname, if any
 * @error       : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the hostname of the remote session the session has, if any.
 * Free the remote_host string with g_free when finished.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_remote_host (LibConsoleKit *ck,
                                        const gchar *session,
                                        gchar **remote_host,
                                        GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (remote_host == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "remote_host must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetRemoteHostName",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", remote_host);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_session_get_tty:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @tty     : (out) (transfer full): The tty attached to the session
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the tty the session has, if any.
 * Free the tty string with g_free when finished.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_tty (LibConsoleKit *ck,
                                const gchar *session,
                                gchar **tty,
                                GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (tty == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "tty must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetX11DisplayDevice",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        if (variant == NULL) {
                g_clear_object(&session_proxy);
                return FALSE;
        }

        g_variant_get_child (variant, 0, "s", tty);

        g_variant_unref (variant);
        variant = NULL;

        /* We're not running X11, try for just the display device */
        if (strlen (*tty) == 0) {
                g_free (*tty);

                variant = g_dbus_proxy_call_sync (session_proxy,
                                                  "GetDisplayDevice",
                                                  g_variant_new ("()"),
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  NULL,
                                                  error);

                if (variant == NULL) {
                        g_clear_object(&session_proxy);
                        return FALSE;
                }

                g_variant_get_child (variant, 0, "s", tty);

                g_variant_unref (variant);
                variant = NULL;
        }

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        return TRUE;
}

/**
 * lib_consolekit_session_get_vt:
 * @ck      : A #LibConsoleKit
 * @session : The session to query
 * @vt      : (out) : The VT of the session, if any
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the VT the session is on, if any.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_session_get_vt (LibConsoleKit *ck,
                                const gchar *session,
                                guint *vt,
                                GError **error)
{
        GDBusProxy *session_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        if (vt == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "vt must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit session */
        session_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       session,
                                                       CK_SESSION_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (session_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (session_proxy,
                                          "GetVTNr",
                                          g_variant_new ("()"),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the session proxy */
        g_clear_object(&session_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "u", vt);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}

/**
 * lib_consolekit_pid_get_session:
 * @ck      : A #LibConsoleKit
 * @pid     : process to query
 * @session : (out) (transfer full): The session the pid belongs to
 * @error   : (out) (allow-none) (transfer full): The error message if something failed
 *
 * Returns the session the pid belongs to, if any.
 * Free the session string with g_free when finished.
 *
 * Return value: TRUE on Success.
 *
 * Since: 1.0
 **/
gboolean
lib_consolekit_pid_get_session (LibConsoleKit *ck,
                                pid_t pid,
                                gchar **session,
                                GError **error)
{
        GDBusProxy *manager_proxy = NULL;
        GVariant   *variant = NULL;

        if (ck == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Invalid LibConsoleKit");
                return FALSE;
        }

        if (session == NULL) {
                g_set_error (error,
                             CONSOLEKIT_ERROR,
                             CONSOLEKIT_ERROR_INVALID_INPUT,
                             "Session must not be NULL");
                return FALSE;
        }

        /* connect to the ConsoleKit manager */
        manager_proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                       G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES | G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                       NULL,
                                                       CK_NAME,
                                                       CK_MANAGER_PATH,
                                                       CK_MANAGER_NAME,
                                                       NULL,
                                                       error);

        /* failed to connect */
        if (manager_proxy == NULL) {
                return FALSE;
        }

        variant = g_dbus_proxy_call_sync (manager_proxy,
                                          "GetSessionByPID",
                                          g_variant_new ("(u)", pid),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,
                                          NULL,
                                          error);

        /* We're done with the manager proxy */
        g_clear_object(&manager_proxy);

        if (variant == NULL) {
                return FALSE;
        }

        g_variant_get_child (variant, 0, "o", session);

        g_variant_unref (variant);
        variant = NULL;

        return TRUE;
}
