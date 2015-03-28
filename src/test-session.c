/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Eric Koegel <eric.koegel@gmail.com>
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

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <gio/gio.h>

#include "ck-session.h"


CkSession *session;
GDBusProxy *proxy;
ConsoleKitSession *cksession;
static GMainLoop *loop;

#define DBUS_NAME "org.freedesktop.ConsoleKit.TestSession"
#define DBUS_PATH "/org/freedesktop/ConsoleKit/TestSession"


static void
test_unref_session (void)
{
    /* Verify we have an object */
    g_assert (session != NULL);
    g_assert (CK_IS_SESSION (session));

    g_object_unref (session);
    session = NULL;
}

static void
test_set_some_stuff (void)
{
    g_print ("console_kit_session_call_activate_sync\n");

    console_kit_session_call_activate_sync (cksession, NULL, NULL);

    g_print ("test_set_some_stuff\n");

    console_kit_session_set_unix_user (cksession, 1000);
    console_kit_session_set_session_type (cksession, "graphical");
    console_kit_session_set_remote_host_name (cksession, "test-client");
    console_kit_session_set_display_device (cksession, "/dev/tty64");
    console_kit_session_set_is_local (cksession, TRUE);
}

static gboolean
test_validate_stuff (gpointer user_data)
{
    g_print ("test_validate_stuff\n");

    if (cksession == NULL)
        return FALSE;

    g_print ("unix user: %d\n", console_kit_session_get_unix_user (cksession));
    g_print ("session type: %s\n", console_kit_session_get_session_type (cksession));
    g_print ("remote hostname: %s\n", console_kit_session_get_remote_host_name (cksession));
    g_print ("display device: %s\n", console_kit_session_get_display_device (cksession));
    g_print ("is active? %s\n", console_kit_session_get_active (cksession) ? "TRUE" : "FALSE");
    g_print ("is local? %s\n", console_kit_session_get_is_local (cksession) ? "TRUE" : "FALSE");
    g_print ("done printing stuff\n\n");

    return TRUE;
}

static void
test_setup_cksession_proxy (void)
{
    GError *error = NULL;

    g_print ("test_setup_cksession_proxy\n");

    cksession = console_kit_session_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                            G_BUS_NAME_OWNER_FLAGS_NONE,
                                                            DBUS_NAME, DBUS_PATH,
                                                            NULL,
                                                            &error);

    if (cksession == NULL)
    {
        g_printerr ("Error creating cksession proxy: %s\n", error->message);
        g_error_free (error);
    }

    g_timeout_add_seconds (10, test_validate_stuff, NULL);
}

static void
print_properties (GDBusProxy *proxy)
{
  gchar **property_names;
  guint n;

  g_print ("    properties:\n");

  property_names = g_dbus_proxy_get_cached_property_names (proxy);
  for (n = 0; property_names != NULL && property_names[n] != NULL; n++)
    {
      const gchar *key = property_names[n];
      GVariant *value;
      gchar *value_str;
      value = g_dbus_proxy_get_cached_property (proxy, key);
      value_str = g_variant_print (value, TRUE);
      g_print ("      %s -> %s\n", key, value_str);
      g_variant_unref (value);
      g_free (value_str);
    }
  g_strfreev (property_names);
}

static void
on_properties_changed (GDBusProxy          *proxy,
                       GVariant            *changed_properties,
                       const gchar* const  *invalidated_properties,
                       gpointer             user_data)
{
  /* Note that we are guaranteed that changed_properties and
   * invalidated_properties are never NULL
   */

    g_print ("on_properties_changed\n");

  if (g_variant_n_children (changed_properties) > 0)
    {
      GVariantIter *iter;
      const gchar *key;
      GVariant *value;

      g_print (" *** Properties Changed:\n");
      g_variant_get (changed_properties,
                     "a{sv}",
                     &iter);
      while (g_variant_iter_loop (iter, "{&sv}", &key, &value))
        {
          gchar *value_str;
          value_str = g_variant_print (value, TRUE);
          g_print ("      %s -> %s\n", key, value_str);
          g_free (value_str);
        }
      g_variant_iter_free (iter);
    }

  if (g_strv_length ((GStrv) invalidated_properties) > 0)
    {
      guint n;
      g_print (" *** Properties Invalidated:\n");
      for (n = 0; invalidated_properties[n] != NULL; n++)
        {
          const gchar *key = invalidated_properties[n];
          g_print ("      %s\n", key);
        }
    }
}

static void
on_signal (GDBusProxy *proxy,
           gchar      *sender_name,
           gchar      *signal_name,
           GVariant   *parameters,
           gpointer    user_data)
{
    gchar *parameters_str;

    g_print ("on_signal\n");

    parameters_str = g_variant_print (parameters, TRUE);
    g_print (" *** Received Signal: %s: %s\n",
             signal_name,
             parameters_str);
    g_free (parameters_str);
}

static void
print_proxy (GDBusProxy *proxy)
{
    gchar *name_owner;

    g_print ("print_proxy\n");

    name_owner = g_dbus_proxy_get_name_owner (proxy);
    if (name_owner != NULL)
    {
        g_print ("+++ Proxy object points to remote object owned by %s\n"
                 "    bus:          %s\n"
                 "    name:         %s\n"
                 "    object path:  %s\n"
                 "    interface:    %s\n",
                 name_owner,
                 "Session Bus",
                 DBUS_NAME,
                 DBUS_PATH,
                 "org.freedesktop.ConsoleKit.Session");
        print_properties (proxy);
    }
    else
    {
        g_print ("--- Proxy object is inert - there is no name owner for the name\n"
                 "    bus:          %s\n"
                 "    name:         %s\n"
                 "    object path:  %s\n"
                 "    interface:    %s\n",
                 "Session Bus",
                 DBUS_NAME,
                 DBUS_PATH,
                 "org.freedesktop.ConsoleKit.Session");
    }
    g_free (name_owner);

    test_setup_cksession_proxy ();
    test_set_some_stuff ();
}

static void
on_name_owner_notify (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (object);
  g_print ("on_name_owner_notify\n");
  print_proxy (proxy);
}

static gboolean
test_setup_proxy (gpointer user_data)
{
    GError *error = NULL;

    g_print ("test_setup_proxy\n");

    proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                           G_BUS_NAME_OWNER_FLAGS_NONE,
                                           NULL, /* GDBusInterfaceInfo */
                                           DBUS_NAME,
                                           DBUS_PATH,
                                           "org.freedesktop.ConsoleKit.Session",
                                           NULL, /* GCancellable */
                                           &error);

    if (proxy == NULL)
    {
        g_printerr ("Error creating proxy: %s\n", error->message);
        g_error_free (error);
        return FALSE;
    }

    g_signal_connect (proxy,
                      "g-properties-changed",
                      G_CALLBACK (on_properties_changed),
                      NULL);
    g_signal_connect (proxy,
                      "g-signal",
                      G_CALLBACK (on_signal),
                      NULL);
    g_signal_connect (proxy,
                      "notify::g-name-owner",
                      G_CALLBACK (on_name_owner_notify),
                      NULL);
    print_proxy (proxy);

    return FALSE;
}

static void
bus_acquired (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
    g_print ("bus_acquired\n");

    session = ck_session_new (DBUS_PATH, "cookie!", connection);

    /* Verify we got a valid object */
    g_assert (session != NULL);
    g_assert (CK_IS_SESSION (session));
}

static void
name_acquired (GDBusConnection *connection,
               const gchar *name,
               gpointer user_data)
{
    g_print ("name_acquired\n");

    g_timeout_add_seconds (4, test_setup_proxy, NULL);
}

static void
name_lost (GDBusConnection *connection,
           const gchar *name,
           gpointer user_data)
{
    g_print ("name_lost\n");

    /* Release the  object */
    test_unref_session ();
}

static void
test_own_bus (void)
{
    g_bus_own_name (G_BUS_TYPE_SESSION, DBUS_NAME,
                    G_BUS_NAME_OWNER_FLAGS_NONE,
                    bus_acquired, name_acquired, name_lost,
                    NULL, NULL);
}

int
main (int   argc,
      char *argv[])
{
    /* do not run these tests as root */
    if (getuid () == 0) {
            g_warning ("You must NOT be root to run these tests");
            exit (1);
    }

    g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
              g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

    loop = g_main_loop_new (NULL, FALSE);

    session = NULL;

    test_own_bus ();

    g_main_loop_run (loop);

    g_object_unref (loop);

    return 0;
}
