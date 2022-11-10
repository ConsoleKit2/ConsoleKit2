#include "config.h"

#include <sys/types.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "libconsolekit.h"

int
sd_session_get_class(const char *session, char **class)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_class (ck, session, class, &error);
	if (error) {
		g_warning ("Unable to determine session class: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_session_get_seat(const char *session, char **seat)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_seat (ck, session, seat, &error);
	if (error) {
		g_warning ("Unable to determine seat: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_session_get_type(const char *session, char **type)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_type (ck, session, type, &error);
	if (error)  {
		g_warning ("Unable to determine seat type: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_session_get_state(const char *session, char **state)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_state (ck, session, state, &error);
	if (error)  {
		g_warning ("Unable to determine seat state: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_session_get_uid(const char *session, uid_t *uid)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_uid (ck, session, uid, &error);
	if (error)  {
		g_warning ("Unable to determine session uid: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_uid_get_sessions(uid_t uid, int require_active, char ***sessions)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	int ret = 0;

	ck = lib_consolekit_new ();

	ret = lib_consolekit_uid_get_sessions (ck, uid, sessions, &error);
	if (error)  {
		g_warning ("Unable to determine session uid: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return ret;
}

int
sd_seat_can_multi_session(const char *seat)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	gboolean can_activate = FALSE;

	ck = lib_consolekit_new ();

	can_activate = lib_consolekit_seat_can_multi_session (ck, seat, &error);
	if (error) {
		g_warning ("Unable to determine if seat can activate sessions: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return FALSE;
	}

	g_object_unref (ck);

	return can_activate;
}

int
sd_get_sessions(char ***sessions)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	int ret = 0;

	ck = lib_consolekit_new ();

	ret = lib_consolekit_get_sessions (ck, sessions, &error);
	if (error)  {
		g_warning ("Unable to get sessions: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return ret;
}
