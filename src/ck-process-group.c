/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2015, Eric Koegel <eric.koegel@gmail.com>
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

/* Note to porters, on Linux, cgroups are used here only to tag the session
 * leader process with a string, the ssid. In doing so, the kernel will
 * also tag any decendants of that process (via clone, fork, whatever) as well.
 * This way even if the process does things like double-forks or forgets to
 * pass along the XDG_SESSION_COOKIE, it and it's decendants always have
 * that ssid so ConsoleKit2 doesn't get confused. We don't need or use
 * anything else with cgroups such as resource management.
 */

#include "config.h"

#include <pwd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#ifdef HAVE_CGMANAGER
#include <cgmanager/cgmanager.h>
#include <cgmanager/cgmanager-client.h>
#include <nih/alloc.h>
#include <nih/error.h>
#include <nih/string.h>
#endif

/* For TRACE */
#include "ck-sysdeps.h"

#include "ck-process-group.h"

#define CK_PROCESS_GROUP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_PROCESS_GROUP, CkProcessGroupPrivate))

struct CkProcessGroupPrivate
{
        gint          unused;
#ifdef HAVE_CGMANAGER
        NihDBusProxy *cgmanager_proxy;
#endif
};

static void     ck_process_group_class_init  (CkProcessGroupClass *klass);
static void     ck_process_group_init        (CkProcessGroup      *pgroup);
static void     ck_process_group_finalize    (GObject             *object);

G_DEFINE_TYPE (CkProcessGroup, ck_process_group, G_TYPE_OBJECT)


#ifdef HAVE_CGMANAGER
/* Ensure the warning message contains a %s to handle the actual warning
 * text from libnih
 */
static void
throw_nih_warning (const gchar *warning)
{
        NihError *nerr = nih_error_get ();
        if (nerr != NULL) {
                g_warning (warning, nerr->message);
                nih_free (nerr);
        }
}
#endif

static gboolean
ck_process_group_backend_init (CkProcessGroup *pgroup)
{
#ifdef HAVE_CGMANAGER
        DBusError       dbus_error;
        DBusConnection *connection = NULL;

        TRACE ();

        dbus_error_init (&dbus_error);

        /* cgmanager uses a dbus based socket rather than running on the
         * system bus, nify. Let's connect to it.
         */
        connection = dbus_connection_open_private (CGMANAGER_DBUS_PATH, &dbus_error);
        if (!connection) {
                /* TRANSLATORS: This is letting the user know that cgmanager
                 * support was compiled in, but the cgmanager daemon isn't
                 * running.
                 */
                g_warning (_("Failed to open connection to cgmanager. Is the cgmanager daemon running?"));
                dbus_error_free (&dbus_error);
                return FALSE;
        }

        dbus_connection_set_exit_on_disconnect (connection, FALSE);
        dbus_error_free (&dbus_error);

        pgroup->priv->cgmanager_proxy = nih_dbus_proxy_new (NULL, connection, NULL, "/org/linuxcontainers/cgmanager", NULL, NULL);
        dbus_connection_unref (connection);
        if (!pgroup->priv->cgmanager_proxy) {
                /* TRANSLATORS: There is an error with cgmanager, we're just
                 * printing it out. Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("There was an error while initializing cgmanager, the error was: %s"));
                return FALSE;
        }
#endif
        return TRUE;
}

/**
 * ck_process_group_get:
 *
 * Increases the reference count of the @CkProcessGroup object.
 *
 * Return value:  Returns the CkProcessGroup object or
 *                NULL on failure. Do not unref when finished. [transfer: none]
 **/
CkProcessGroup*
ck_process_group_get (void)
{
        static GObject *manager = NULL;

        if (manager == NULL) {
                manager = g_object_new (CK_TYPE_PROCESS_GROUP, NULL);

                g_object_add_weak_pointer (manager,
                                           (gpointer *) &manager);

                ck_process_group_backend_init (CK_PROCESS_GROUP (manager));
        }

        return CK_PROCESS_GROUP (manager);
}

static void
ck_process_group_class_init (CkProcessGroupClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_process_group_finalize;

        g_type_class_add_private (klass, sizeof (CkProcessGroupPrivate));
}

static void
ck_process_group_init (CkProcessGroup *pgroup)
{
        pgroup->priv = CK_PROCESS_GROUP_GET_PRIVATE (pgroup);
}

static void
ck_process_group_finalize (GObject *object)
{
#ifdef HAVE_CGMANAGER
        CkProcessGroupPrivate *priv = CK_PROCESS_GROUP_GET_PRIVATE (object);

        TRACE ();

        if (priv->cgmanager_proxy) {
                dbus_connection_flush(priv->cgmanager_proxy->connection);
                dbus_connection_close(priv->cgmanager_proxy->connection);
                nih_free(priv->cgmanager_proxy);
        }
        priv->cgmanager_proxy = NULL;
#endif

        G_OBJECT_CLASS (ck_process_group_parent_class)->finalize (object);
}

/**
 * ck_process_group_create:
 * @CkProcessGroup: the pgroup object.
 * @process: the process to add to the new group
 * @ssid: the session id.
 *
 * Creates a new process group named @ssid and places @process inside it.
 *
 * Return value:  TRUE on success, FALSE if process groups are unsupported
 *                on this platform.
 **/
gboolean
ck_process_group_create (CkProcessGroup *pgroup,
                         pid_t process,
                         const gchar *ssid)
{
#ifdef HAVE_CGMANAGER
        CkProcessGroupPrivate *priv = CK_PROCESS_GROUP_GET_PRIVATE (pgroup);
        gint                   ret;
        gint32                 existed;
        struct passwd         *pwent;

        TRACE ();

        if (priv->cgmanager_proxy == NULL) {
                g_debug ("cgmanager_proxy == NULL");
                return FALSE;
        }

        /* Create the cgroup, move the pid into it, and then tell cgmanager
         * to clean up the cgroup after all the processes are gone which
         * will happen when the user logs out.
         */
        ret = cgmanager_create_sync (NULL, priv->cgmanager_proxy, "all", ssid, &existed);
        if (ret != 0) {
                /* TRANSLATORS: Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("Failed to create cgroup, the error was: %s"));
                return FALSE;
        }

        errno = 0;
        pwent = getpwuid (ck_unix_pid_get_uid(process));
        if (pwent == NULL) {
                g_warning ("Unable to lookup UID: %s", g_strerror (errno));
                return FALSE;
        }

        ret = cgmanager_chown_sync(NULL, priv->cgmanager_proxy, "all", ssid, pwent->pw_uid, pwent->pw_gid);
        if (ret != 0) {
                /* TRANSLATORS: Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("Failed to change owner of the new cgroup to owner of the session leader, the error was: %s"));
                return FALSE;
        }

        ret = cgmanager_move_pid_abs_sync (NULL, priv->cgmanager_proxy, "all", ssid, process);
        if (ret != 0) {
                /* TRANSLATORS: Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("Failed to move the session leader process to cgroup, the error was: %s"));

                /* We failed to move the process into all the cgroups, but
                 * we really only require the cpuacct for our internal use.
                 * So try that as a fallback now.
                 */
                ret = cgmanager_move_pid_abs_sync (NULL, priv->cgmanager_proxy, "cpuacct", ssid, process);
                if (ret != 0) {
                        /* TRANSLATORS: Please ensure you keep the %s in the
                         * string somewhere. It's the detailed error message from
                         * cgmanager.
                         */
                        throw_nih_warning (_("Failed to move the session leader process to 'cpuacct' cgroup, the error was: %s"));
                }
        }

        ret = cgmanager_remove_on_empty_sync (NULL, priv->cgmanager_proxy, "all", ssid);
        if (ret != 0) {
                /* TRANSLATORS: Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("Failed to let cgmanager know that it can remove the cgroup when it's empty, the error was: %s"));
                /* this is not an issue if it fails */
        }

        return TRUE;
#endif
        return FALSE;
}

/**
 * ck_process_group_get_ssid:
 * @CkProcessGroup: the pgroup object.
 * @process: the process to add to the new group
 *
 * Return value: the ssid of the pid, otherwise NULL
 **/
gchar*
ck_process_group_get_ssid (CkProcessGroup *pgroup,
                           pid_t process)
{
#ifdef HAVE_CGMANAGER
        CkProcessGroupPrivate *priv = CK_PROCESS_GROUP_GET_PRIVATE (pgroup);
        gint                   ret;
        char                  *nih_ssid = NULL;
        gchar                 *g_ssid = NULL;

        TRACE ();

        if (priv->cgmanager_proxy == NULL) {
                g_debug ("cgmanager_proxy == NULL");
                return NULL;
        }

        ret = cgmanager_get_pid_cgroup_abs_sync (NULL, priv->cgmanager_proxy, "cpuacct", process, &nih_ssid);
        if (ret != 0) {
                /* TRANSLATORS: Please ensure you keep the %s in the
                 * string somewhere. It's the detailed error message from
                 * cgmanager.
                 */
                throw_nih_warning (_("Failed to get the session id from cgmanager, the error was: %s"));
                return NULL;
        }

        /* This is probably why you don't mix toolkits. So memory allocated
         * with nih causes corruption issues with glib, so copy + free it */
        if (nih_ssid != NULL) {
                g_ssid = g_strdup (nih_ssid);

                nih_free (nih_ssid);
        }

        /* ignore the unknown/root cgroup */
        if (g_strcmp0 (g_ssid, "/") == 0) {
                g_free (g_ssid);
                g_ssid = NULL;
        }

        return g_ssid;
#endif
        return NULL;
}

