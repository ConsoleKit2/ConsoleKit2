/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * ck-connector.c : Code for login managers to register with ConsoleKit.
 *
 * Copyright (c) 2007 David Zeuthen <davidz@redhat.com>
 * Copyright (c) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dbus/dbus.h>

#include "ck-connector.h"

#if defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define _CK_FUNCTION_NAME __func__
#elif defined(__GNUC__) || defined(_MSC_VER)
#define _CK_FUNCTION_NAME __FUNCTION__
#else
#define _CK_FUNCTION_NAME "unknown function"
#endif

#define CK_CONNECTOR_ERROR "org.freedesktop.CkConnector.Error"

#define _CK_WARNING_FORMAT "arguments to %s() were incorrect, assertion \"%s\" failed in file %s line %d.\n"
#define _ck_return_if_fail(condition) do {                                         \
  if (!(condition)) {                                                              \
          fprintf (stderr, _CK_WARNING_FORMAT, _CK_FUNCTION_NAME, #condition, __FILE__, __LINE__); \
    return;                                                                        \
  } } while (0)

#define _ck_return_val_if_fail(condition, val) do {                                     \
  if (!(condition)) {                                                              \
          fprintf (stderr, _CK_WARNING_FORMAT, _CK_FUNCTION_NAME, #condition, __FILE__, __LINE__); \
    return val;                                                                        \
  } } while (0)

struct _CkConnector
{
        int             refcount;
        char           *cookie;
        dbus_bool_t     session_created;
        DBusConnection *connection;
};

static dbus_bool_t
add_param_int32 (DBusMessageIter *iter_array,
                 const char      *key,
                 dbus_int32_t     value)
{
        DBusMessageIter iter_struct;
        DBusMessageIter iter_variant;

        if (! dbus_message_iter_open_container (iter_array,
                                                DBUS_TYPE_STRUCT,
                                                NULL,
                                                &iter_struct)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_struct,
                                              DBUS_TYPE_STRING,
                                              &key)) {
                goto oom;
        }

        if (! dbus_message_iter_open_container (&iter_struct,
                                                DBUS_TYPE_VARIANT,
                                                DBUS_TYPE_INT32_AS_STRING,
                                                &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_variant,
                                              DBUS_TYPE_INT32,
                                              &value)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (&iter_struct,
                                                 &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (iter_array,
                                                 &iter_struct)) {
                goto oom;
        }

        return TRUE;
oom:
        return FALSE;
}

static dbus_bool_t
add_param_string (DBusMessageIter *iter_array,
                  const char      *key,
                  const char      *value)
{
        DBusMessageIter iter_struct;
        DBusMessageIter iter_variant;

        if (! dbus_message_iter_open_container (iter_array,
                                                DBUS_TYPE_STRUCT,
                                                NULL,
                                                &iter_struct)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_struct,
                                              DBUS_TYPE_STRING,
                                              &key)) {
                goto oom;
        }

        if (! dbus_message_iter_open_container (&iter_struct,
                                                DBUS_TYPE_VARIANT,
                                                DBUS_TYPE_STRING_AS_STRING,
                                                &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_variant,
                                              DBUS_TYPE_STRING,
                                              &value)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (&iter_struct,
                                                 &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (iter_array,
                                                 &iter_struct)) {
                goto oom;
        }

        return TRUE;
oom:
        return FALSE;
}

static dbus_bool_t
add_param_bool (DBusMessageIter *iter_array,
                const char      *key,
                dbus_bool_t      value)
{
        DBusMessageIter iter_struct;
        DBusMessageIter iter_variant;

        if (! dbus_message_iter_open_container (iter_array,
                                                DBUS_TYPE_STRUCT,
                                                NULL,
                                                &iter_struct)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_struct,
                                              DBUS_TYPE_STRING,
                                              &key)) {
                goto oom;
        }

        if (! dbus_message_iter_open_container (&iter_struct,
                                                DBUS_TYPE_VARIANT,
                                                DBUS_TYPE_BOOLEAN_AS_STRING,
                                                &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_variant,
                                              DBUS_TYPE_BOOLEAN,
                                              &value)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (&iter_struct,
                                                 &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (iter_array,
                                                 &iter_struct)) {
                goto oom;
        }

        return TRUE;
oom:
        return FALSE;
}

/* Frees all resources allocated and disconnects from the system
 * message bus.
 */
static void
_ck_connector_free (CkConnector *connector)
{
        if (connector->connection != NULL) {
                /* it's a private connection so it's all good */
                dbus_connection_close (connector->connection);
        }

        if (connector->cookie != NULL) {
                free (connector->cookie);
        }

        free (connector);
}

/**
 * Decrements the reference count of a CkConnector, disconnecting
 * from the bus and freeing the connector if the count reaches 0.
 *
 * @param connector the connector
 * @see ck_connector_ref
 */
void
ck_connector_unref (CkConnector *connector)
{
        _ck_return_if_fail (connector != NULL);

        /* Probably should use some kind of atomic op here */
        connector->refcount -= 1;
        if (connector->refcount == 0) {
                _ck_connector_free (connector);
        }
}

/**
 * Increments the reference count of a CkConnector.
 *
 * @param connector the connector
 * @returns the connector
 * @see ck_connector_unref
 */
CkConnector *
ck_connector_ref (CkConnector *connector)
{
        _ck_return_val_if_fail (connector != NULL, NULL);

        /* Probably should use some kind of atomic op here */
        connector->refcount += 1;

        return connector;
}

/**
 * Constructs a new Connector to communicate with the ConsoleKit
 * daemon. Returns #NULL if memory can't be allocated for the
 * object.
 *
 * @returns a new CkConnector, free with ck_connector_unref()
 */
CkConnector *
ck_connector_new (void)
{
        CkConnector *connector;

        connector = calloc (1, sizeof (CkConnector));
        if (connector == NULL) {
                goto oom;
        }

        connector->refcount = 1;
        connector->connection = NULL;
        connector->cookie = NULL;
        connector->session_created = FALSE;
oom:
        return connector;
}

/**
 * Connects to the D-Bus system bus daemon and issues the method call
 * OpenSession on the ConsoleKit manager interface. The
 * connection to the bus is private.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 *
 * @returns #TRUE if the operation succeeds
 */
dbus_bool_t
ck_connector_open_session (CkConnector *connector,
                           DBusError   *out_error)
{
        DBusError    error;
        DBusMessage *message;
        DBusMessage *reply;
        dbus_bool_t  ret;
        char        *cookie;

        _ck_return_val_if_fail (connector != NULL, FALSE);

        reply = NULL;
        message = NULL;
        ret = FALSE;

        dbus_error_init (&error);
        connector->connection = dbus_bus_get_private (DBUS_BUS_SYSTEM, &error);
        if (connector->connection == NULL) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                }

                goto out;
        }

        dbus_connection_set_exit_on_disconnect (connector->connection, FALSE);

        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "OpenSession");
        if (message == NULL) {
                goto out;
        }

        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &error);
        if (reply == NULL) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        if (! dbus_message_get_args (reply,
                                     &error,
                                     DBUS_TYPE_STRING, &cookie,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        connector->cookie = strdup (cookie);
        if (connector->cookie == NULL) {
                goto out;
        }

        connector->session_created = TRUE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;
}

/**
 * Connects to the D-Bus system bus daemon and issues the method call
 * OpenSessionWithParameters on the ConsoleKit manager interface. The
 * connection to the bus is private.
 *
 * The only parameter that is optional is x11_display - it may be set
 * to NULL if there is no X11 server associated with the session.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 *
 * @param user UID for the user owning the session
 * @param tty the tty device for the session
 * @param x11_display the value of the X11 DISPLAY for the session
 * @returns #TRUE if the operation succeeds
 */
dbus_bool_t
ck_connector_open_session_for_user (CkConnector *connector,
                                    uid_t        user,
                                    const char  *tty,
                                    const char  *x11_display,
                                    DBusError   *out_error)
{
        DBusError       error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        DBusMessageIter iter_array;
        dbus_bool_t     ret;
        char           *cookie;

        _ck_return_val_if_fail (connector != NULL, FALSE);
        _ck_return_val_if_fail (user > 0, FALSE);
        _ck_return_val_if_fail (tty != NULL, FALSE);

        reply = NULL;
        message = NULL;
        ret = FALSE;

        dbus_error_init (&error);
        connector->connection = dbus_bus_get_private (DBUS_BUS_SYSTEM, &error);
        if (connector->connection == NULL) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                }
                goto out;
        }

        dbus_connection_set_exit_on_disconnect (connector->connection, FALSE);

        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "OpenSessionWithParameters");
        if (message == NULL) {
                goto out;
        }

        dbus_message_iter_init_append (message, &iter);
        if (! dbus_message_iter_open_container (&iter,
                                                DBUS_TYPE_ARRAY,
                                                "(sv)",
                                                &iter_array)) {
                goto out;
        }

        if (! add_param_string (&iter_array, "host-name", "localhost")) {
                goto out;
        }

        if (! add_param_string (&iter_array, "display-device", tty)) {
                goto out;
        }

        if (x11_display != NULL) {
                if (! add_param_string (&iter_array, "x11-display", x11_display)) {
                        goto out;
                }
        }

        if (! add_param_int32 (&iter_array, "user", user)) {
                goto out;
        }

        if (! add_param_bool (&iter_array, "is-local", TRUE)) {
                goto out;
        }

        if (! dbus_message_iter_close_container (&iter, &iter_array)) {
                goto out;
        }

        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &error);
        if (reply == NULL) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        if (! dbus_message_get_args (reply,
                                     &error,
                                     DBUS_TYPE_STRING, &cookie,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        connector->cookie = strdup (cookie);
        if (connector->cookie == NULL) {
                goto out;
        }

        connector->session_created = TRUE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;
}

/**
 * Gets the cookie for the current open session.
 * Returns #NULL if no session is open.
 *
 * @returns a constant string with the cookie.
 */
const char *
ck_connector_get_cookie (CkConnector *connector)
{
        _ck_return_val_if_fail (connector != NULL, NULL);

        if (! connector->session_created) {
                return NULL;
        } else {
                return connector->cookie;
        }
}

/**
 * Issues the CloseSession method call on the ConsoleKit manager
 * interface.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running, if the caller doesn't have
 * sufficient privilege or if a session isn't open.
 *
 * @returns #TRUE if the operation succeeds
 */
dbus_bool_t
ck_connector_close_session (CkConnector *connector,
                            DBusError   *out_error)
{
        DBusError    error;
        DBusMessage *message;
        DBusMessage *reply;
        dbus_bool_t  ret;
        dbus_bool_t  session_closed;

        reply = NULL;
        message = NULL;
        ret = FALSE;

        if (!connector->session_created || connector->cookie == NULL) {
                dbus_set_error (out_error,
                                CK_CONNECTOR_ERROR,
                                "Unable to close session: %s",
                                "no session open");
                goto out;
        }

        dbus_error_init (&error);
        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "CloseSession");
        if (message == NULL) {
                goto out;
        }

        if (! dbus_message_append_args (message,
                                        DBUS_TYPE_STRING, &(connector->cookie),
                                        DBUS_TYPE_INVALID)) {
                goto out;
        }

        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &error);
        if (reply == NULL) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to close session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        if (! dbus_message_get_args (reply,
                                     &error,
                                     DBUS_TYPE_BOOLEAN, &session_closed,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&error)) {
                        dbus_set_error (out_error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to close session: %s",
                                        error.message);
                        dbus_error_free (&error);
                        goto out;
                }
        }

        if (! session_closed) {
                goto out;
        }

        connector->session_created = FALSE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;

}
