/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2022, Robert Nagy <robert@openbsd.org>
 * Copyright (c) 2023, Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *                     Author: Gleb Popov <arrowd@FreeBSD.org>
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

#include <sys/types.h>
#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#ifdef HAVE_SYS_EVENT_H
#include <sys/event.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "libconsolekit.h"
#include "sd-login.h"

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
sd_session_get_service(const char *session, char **service)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_service (ck, session, service, &error);
	if (error)  {
		g_warning ("Unable to determine session service: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}

int
sd_session_get_display(const char *session, char **service)
{
	LibConsoleKit *ck = NULL;
	g_autoptr(GError) error = NULL;

	ck = lib_consolekit_new ();

	lib_consolekit_session_get_display (ck, session, service, &error);
	if (error)  {
		g_warning ("Unable to determine session service: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return 0;
}


int
sd_pid_get_session(pid_t pid, char **session)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	int ret = 0;

	ck = lib_consolekit_new ();

	ret = lib_consolekit_pid_get_session (ck, pid, session, &error);
	if (error)  {
		g_warning ("Unable to determine list of session for pid: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return -ENXIO;
	}

	g_object_unref (ck);

	return ret;
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
sd_seat_can_graphical(const char *seat)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	gboolean can_graphical = FALSE;

	ck = lib_consolekit_new ();

	can_graphical = lib_consolekit_seat_can_graphical (ck, seat, &error);
	if (error) {
		g_warning ("Unable to determine if seat can activate sessions: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return FALSE;
	}

	g_object_unref (ck);

	return can_graphical;
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
sd_seat_get_sessions(const char *seat, char ***sessions,
	uid_t **uid, unsigned int *n_uids)
{
	LibConsoleKit *ck = NULL;
	GError *error = NULL;
	int num_sessions;

	ck = lib_consolekit_new ();

	num_sessions = lib_consolekit_seat_get_sessions (ck, seat, sessions, &error);
	if (error) {
		g_warning ("Unable to determine if seat can activate sessions: %s",
				error ? error->message : "");
		g_error_free (error);
		g_object_unref (ck);
		return FALSE;
	}

	g_object_unref (ck);

	return num_sessions;
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

#if defined(HAVE_SYS_EVENT_H) && defined(EVFILT_USER) && defined(HAVE_POLL_H)
static
void on_signal(GDBusProxy *manager,
				gchar *sender_name,
				gchar *signal_name,
				GVariant *parameters,
				sd_login_monitor *monitor)
{
	struct kevent e;
	if (strcmp(signal_name, "SessionNew") && strcmp(signal_name, "SessionRemoved"))
		return;
	EV_SET(&e, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
	kevent(monitor->kq, &e, 1, 0, 0, 0);
}

int sd_login_monitor_new(const char *category, sd_login_monitor **ret)
{
	struct kevent e;
	GError **error = NULL;
	sd_login_monitor *monitor = NULL;

	monitor = malloc(sizeof(sd_login_monitor));
	if (!monitor)
		return -ENOMEM;

	monitor->kq = kqueue();
	monitor->manager = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
													G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
													NULL,
													CK_NAME,
													CK_MANAGER_PATH,
													CK_MANAGER_NAME,
													NULL,
													error);

	if (monitor->kq == -1 || monitor->manager == NULL)
	{
		sd_login_monitor_unref (monitor);
		return -ENXIO;
	}

	// XXX can't connect to these signals without pulling in ck-manager-generated.h
	// if (!strcmp (category, "session") || category == NULL)
	// {
	// 	g_signal_connect (monitor->manager, "session-new", G_CALLBACK (on_session_changed), monitor);
	// 	g_signal_connect (monitor->manager, "session-removed", G_CALLBACK (on_session_changed), monitor);
	// }

	monitor->category = category;
	g_signal_connect (monitor->manager, "g-signal", G_CALLBACK (on_signal), monitor);

	EV_SET(&e, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
	kevent(monitor->kq, &e, 1, 0, 0, 0);

	*ret = monitor;
	return 0;
}

sd_login_monitor *sd_login_monitor_unref(sd_login_monitor *monitor)
{
	if (!monitor)
		return NULL;

	if (monitor->kq != -1)
		close (monitor->kq);
	if (monitor->manager)
	{
		g_signal_handlers_disconnect_by_func (monitor->manager, on_signal, monitor);
		g_clear_object (&monitor->manager);
	}
	free (monitor);
	return NULL;
}

void sd_login_monitor_unrefp(sd_login_monitor **m)
{
	sd_login_monitor_unref(*m);
}

int sd_login_monitor_flush(sd_login_monitor *monitor)
{
	struct kevent e;
	EV_SET(&e, 0, EVFILT_USER, EV_CLEAR, 0, 0, NULL);
	if (kevent(monitor->kq, &e, 1, 0, 0, 0) == -1)
		return -(errno);
	return 0;
}

int sd_login_monitor_get_fd(sd_login_monitor *monitor)
{
	if (monitor->kq == -1)
		return -EBADF;
	return monitor->kq;
}

int sd_login_monitor_get_events(sd_login_monitor *monitor)
{
	return POLLIN;
}

int sd_login_monitor_get_timeout(sd_login_monitor *monitor, uint64_t *timeout_usec)
{
	return -1;
}

#else

int sd_login_monitor_new(const char *category, sd_login_monitor **ret)
{
	return -ENOTSUP;
}

sd_login_monitor *sd_login_monitor_unref(sd_login_monitor *monitor)
{
	return NULL;
}

void sd_login_monitor_unrefp(sd_login_monitor **m)
{
}

int sd_login_monitor_flush(sd_login_monitor *monitor)
{
	return -ENOTSUP;
}

int sd_login_monitor_get_fd(sd_login_monitor *monitor)
{
	return -ENOTSUP;
}

int sd_login_monitor_get_events(sd_login_monitor *monitor)
{
	return 0;
}

int sd_login_monitor_get_timeout(sd_login_monitor *monitor, uint64_t *timeout_usec)
{
	return -1;
}

#endif
