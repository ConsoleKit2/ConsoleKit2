/*
 * ck-connector.c : Code for login managers to register with ConsoleKit.
 *
 * Copyright (c) 2007 David Zeuthen <davidz@redhat.com>
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

static dbus_bool_t
add_param_int32 (DBusMessageIter *iter_array, const char *key, dbus_int32_t value)
{
	DBusMessageIter iter_struct;
	DBusMessageIter iter_variant;

	if (!dbus_message_iter_open_container (iter_array,
					       DBUS_TYPE_STRUCT,
					       NULL,
					       &iter_struct))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_struct, DBUS_TYPE_STRING, &key))
		goto oom;
	if (!dbus_message_iter_open_container (&iter_struct,
					       DBUS_TYPE_VARIANT,
					       DBUS_TYPE_INT32_AS_STRING,
					       &iter_variant))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_variant, DBUS_TYPE_INT32, &value))
		goto oom;
	if (!dbus_message_iter_close_container (&iter_struct, &iter_variant))
		goto oom;
	if (!dbus_message_iter_close_container (iter_array, &iter_struct))
		goto oom;

	return TRUE;
oom:
	return FALSE;
}

static dbus_bool_t
add_param_string (DBusMessageIter *iter_array, const char *key, const char *value)
{
	DBusMessageIter iter_struct;
	DBusMessageIter iter_variant;

	if (!dbus_message_iter_open_container (iter_array,
					       DBUS_TYPE_STRUCT,
					       NULL,
					       &iter_struct))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_struct, DBUS_TYPE_STRING, &key))
		goto oom;
	if (!dbus_message_iter_open_container (&iter_struct,
					       DBUS_TYPE_VARIANT,
					       DBUS_TYPE_STRING_AS_STRING,
					       &iter_variant))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_variant, DBUS_TYPE_STRING, &value))
		goto oom;
	if (!dbus_message_iter_close_container (&iter_struct, &iter_variant))
		goto oom;
	if (!dbus_message_iter_close_container (iter_array, &iter_struct))
		goto oom;

	return TRUE;
oom:
	return FALSE;
}

static dbus_bool_t
add_param_bool (DBusMessageIter *iter_array, const char *key, dbus_bool_t value)
{
	DBusMessageIter iter_struct;
	DBusMessageIter iter_variant;

	if (!dbus_message_iter_open_container (iter_array,
					       DBUS_TYPE_STRUCT,
					       NULL,
					       &iter_struct))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_struct, DBUS_TYPE_STRING, &key))
		goto oom;
	if (!dbus_message_iter_open_container (&iter_struct,
					       DBUS_TYPE_VARIANT,
					       DBUS_TYPE_BOOLEAN_AS_STRING,
					       &iter_variant))
		goto oom;
	if (!dbus_message_iter_append_basic (&iter_variant, DBUS_TYPE_BOOLEAN, &value))
		goto oom;
	if (!dbus_message_iter_close_container (&iter_struct, &iter_variant))
		goto oom;
	if (!dbus_message_iter_close_container (iter_array, &iter_struct))
		goto oom;

	return TRUE;
oom:
	return FALSE;
}


struct CKConnector_s
{
	char *cookie;
	dbus_bool_t session_created;
	DBusConnection *con;
};

CKConnector*
ckc_new (void)
{
	CKConnector *ckc;

	ckc = calloc (1, sizeof (CKConnector));
	if (ckc == NULL)
		goto oom;
	ckc->con = NULL;
	ckc->cookie = NULL;
	ckc->session_created = FALSE;
oom:
	return ckc;
};

dbus_bool_t
ckc_create_local_session2 (CKConnector *ckc)
{
	DBusError error;
	DBusMessage *message;
	DBusMessage *reply;
	dbus_bool_t ret;
	char *cookie;
	
	reply = NULL;
	message = NULL;
	ret = FALSE;
	
	dbus_error_init (&error);
	ckc->con = dbus_bus_get_private (DBUS_BUS_SYSTEM, &error);
	if (ckc->con == NULL) {
		goto out;
	}
	dbus_connection_set_exit_on_disconnect (ckc->con, FALSE);
	
	message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit", 
						"/org/freedesktop/ConsoleKit/Manager",
						"org.freedesktop.ConsoleKit.Manager",
						"OpenSession");
	if (message == NULL)
		goto out;
	
	reply = dbus_connection_send_with_reply_and_block (ckc->con, message, -1, &error);
	if (reply == NULL) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	if (!dbus_message_get_args (reply, 
				    &error,
				    DBUS_TYPE_STRING, &cookie,
				    DBUS_TYPE_INVALID)) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	ckc->cookie = strdup (cookie);
	if (ckc->cookie == NULL)
		goto out;

	ckc->session_created = TRUE;
	ret = TRUE;
	
out:
	if (reply != NULL)
		dbus_message_unref (reply);

	if (message != NULL)
		dbus_message_unref (message);
	
	return ret;
}

dbus_bool_t
ckc_create_local_session (CKConnector *ckc, uid_t user, const char *tty, const char *x11_display)
{
	DBusError error;
	DBusMessage *message;
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter iter_array;
	dbus_bool_t ret;
	char *cookie;
	
	reply = NULL;
	message = NULL;
	ret = FALSE;
	
	dbus_error_init (&error);
	ckc->con = dbus_bus_get_private (DBUS_BUS_SYSTEM, &error);
	if (ckc->con == NULL) {
		goto out;
	}
	dbus_connection_set_exit_on_disconnect (ckc->con, FALSE);
	
	message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit", 
						"/org/freedesktop/ConsoleKit/Manager",
						"org.freedesktop.ConsoleKit.Manager",
						"OpenSessionWithParameters");
	if (message == NULL)
		goto out;
	
	dbus_message_iter_init_append (message, &iter);
	if (!dbus_message_iter_open_container (&iter, 
					       DBUS_TYPE_ARRAY,
					       "(sv)", 
					       &iter_array))
		goto out;
	
	if (!add_param_string (&iter_array, "host-name", "localhost"))
		goto out;
	if (!add_param_string (&iter_array, "display-device", tty))
		goto out;
	if (x11_display != NULL)
		if (!add_param_string (&iter_array, "x11-display", x11_display))
			goto out;
	if (!add_param_int32 (&iter_array, "user", user))
		goto out;
	if (!add_param_bool (&iter_array, "is-local", TRUE))
		goto out;
	
	if (!dbus_message_iter_close_container (&iter, &iter_array))
		goto out;
	
	reply = dbus_connection_send_with_reply_and_block (ckc->con, message, -1, &error);
	if (reply == NULL) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	if (!dbus_message_get_args (reply, 
				    &error,
				    DBUS_TYPE_STRING, &cookie,
				    DBUS_TYPE_INVALID)) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	ckc->cookie = strdup (cookie);
	if (ckc->cookie == NULL)
		goto out;

	ckc->session_created = TRUE;
	ret = TRUE;
	
out:
	if (reply != NULL)
		dbus_message_unref (reply);

	if (message != NULL)
		dbus_message_unref (message);
	
	return ret;
}

char *
ckc_get_cookie (CKConnector *ckc)
{
	if (!ckc->session_created)
		return NULL;
	else
		return ckc->cookie;
}

dbus_bool_t
ckc_close_session (CKConnector *ckc)
{
	DBusError error;
	DBusMessage *message;
	DBusMessage *reply;
	dbus_bool_t ret;
	dbus_bool_t session_closed;
	
	reply = NULL;
	message = NULL;
	ret = FALSE;
	if (!ckc->session_created || ckc->cookie == NULL)
		goto out;
	
	dbus_error_init (&error);
	message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit", 
						"/org/freedesktop/ConsoleKit/Manager",
						"org.freedesktop.ConsoleKit.Manager",
						"CloseSession");
	if (message == NULL)
		goto out;

	if (!dbus_message_append_args (message, 
				       DBUS_TYPE_STRING, &(ckc->cookie),
				       DBUS_TYPE_INVALID))
		goto out;
	
	reply = dbus_connection_send_with_reply_and_block (ckc->con, message, -1, &error);
	if (reply == NULL) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	if (!dbus_message_get_args (reply, 
				    &error,
				    DBUS_TYPE_BOOLEAN, &session_closed,
				    DBUS_TYPE_INVALID)) {
		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			goto out;
		}
	}

	if (!session_closed)
		goto out;

	ckc->session_created = FALSE;
	ret = TRUE;
	
out:
	if (reply != NULL)
		dbus_message_unref (reply);

	if (message != NULL)
		dbus_message_unref (message);
	
	return ret;

}

void
ckc_free (CKConnector *ckc)
{
	if (ckc->con != NULL) {
		/* it's a private connection so it's all good */
		dbus_connection_close (ckc->con);
	}
	if (ckc->cookie != NULL) {
		free (ckc->cookie);
	}
	free (ckc);
}
