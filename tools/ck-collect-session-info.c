/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>

#include <glib.h>

#include "ck-sysdeps.h"

typedef struct {
        uid_t    uid;
        pid_t    pid;
        char    *login_session_id;
        char    *display_device;
        char    *x11_display_device;
        char    *x11_display;
        gboolean x11_can_connect;
        char    *remote_host_name;
        gboolean is_local;
        gboolean is_local_is_set;
} SessionInfo;

static void
session_info_free (SessionInfo *si)
{
        g_free (si->login_session_id);
        g_free (si->display_device);
        g_free (si->x11_display_device);
        g_free (si->x11_display);
        g_free (si->remote_host_name);
        g_free (si);
}

static void
setuid_child_setup_func (SessionInfo *si)
{
        int            res;
        struct passwd *pwent;

        errno = 0;
        pwent = getpwuid (si->uid);
        if (pwent == NULL) {
                g_warning ("Unable to lookup UID: %s", g_strerror (errno));
                exit (1);
        }

        /* set the group */
        errno = 0;
        res = setgid (pwent->pw_gid);
        if (res == -1) {
                g_warning ("Error performing setgid: %s", g_strerror (errno));
                exit (1);
        }

        /* become the user */
        errno = 0;
        res = setuid (si->uid);
        if (res == -1) {
                g_warning ("Error performing setuid: %s", g_strerror (errno));
                exit (1);
        }
}

static GPtrArray *
get_filtered_environment (pid_t pid)
{
        GPtrArray  *env;
        GHashTable *hash;
        int         i;
        static const char *allowed_env_vars [] = {
                "DISPLAY",
                "XAUTHORITY",
                "XAUTHLOCALHOSTNAME",
                "SSH_CLIENT",
                "SSH_CONNECTION",
                "SSH_TTY",
                "HOME",
        };

        env = g_ptr_array_new ();

        g_ptr_array_add (env, g_strdup ("PATH=/bin:/usr/bin"));

        hash = ck_unix_pid_get_env_hash (pid);

        for (i = 0; i < G_N_ELEMENTS (allowed_env_vars); i++) {
                const char *var;
                const char *val;
                var = allowed_env_vars [i];
                val = g_hash_table_lookup (hash, var);
                if (val != NULL) {
                        char *str;
                        str = g_strdup_printf ("%s=%s", var, val);
                        g_ptr_array_add (env, str);
                }
        }

        g_ptr_array_add (env, NULL);

        g_hash_table_destroy (hash);

        return env;
}

static void
get_x11_server_pid (SessionInfo *si,
                    gboolean    *can_connect,
                    guint       *pid)
{
        gboolean   res;
        char      *err;
        char      *out;
        int        status;
        int        i;
        GError    *error;
        guint      num;
        char      *argv[4];
        GPtrArray *env;

        if (can_connect != NULL) {
                *can_connect = FALSE;
        }
        if (pid != NULL) {
                *pid = 0;
        }

        /* get the applicable environment */
        env = get_filtered_environment (si->pid);

        num = 0;

        argv[0] = LIBEXECDIR "/ck-get-x11-server-pid";
        argv[1] = NULL;

        error = NULL;
        out = NULL;
        err = NULL;
        status = -1;
        res = g_spawn_sync (NULL,
                            argv,
                            (char **)env->pdata,
                            0,
                            (GSpawnChildSetupFunc)setuid_child_setup_func,
                            si,
                            &out,
                            &err,
                            &status,
                            &error);
        for (i = 0; i < env->len; i++) {
                g_free (g_ptr_array_index (env, i));
        }
        g_ptr_array_free (env, TRUE);

        if (error != NULL) {
                g_warning ("Unable to PID for x11 server: %s", error->message);
                g_error_free (error);
        }

        if (status == 0) {
                if (res && out != NULL) {
                        guint v;
                        char  c;

                        if (1 == sscanf (out, "%u %c", &v, &c)) {
                                num = v;
                        }
                }

                if (can_connect != NULL) {
                        *can_connect = TRUE;
                }
        }


        if (err != NULL && err[0] != '\0') {
                g_warning ("%s", err);
        }

        if (pid != NULL) {
                *pid = num;
        }

        g_free (out);
        g_free (err);
}

/* Looking at the XFree86_VT property on the root window
 * doesn't work very well because it is difficult to
 * distinguish local from remote systems and the value
 * can't necessarily be trusted.  So instead we connect
 * to the server and use peer credentials to find the
 * local PID and then find its tty.
 */
static void
fill_x11_info (SessionInfo *si)
{
        guint          xorg_pid;
        gboolean       can_connect;
        gboolean       res;
        CkProcessStat *xorg_stat;
        GError        *error;

        /* assume this is true then check it */
        si->x11_display = ck_unix_pid_get_env (si->pid, "DISPLAY");

        if (si->x11_display == NULL) {
                /* no point continuing */
                si->x11_can_connect = FALSE;
                return;
        }

        xorg_pid = 0;
        can_connect = FALSE;
        get_x11_server_pid (si, &can_connect, &xorg_pid);

        si->x11_can_connect = can_connect;
        if (! can_connect) {
                g_free (si->x11_display);
                si->x11_display = NULL;
                return;
        }

        if (xorg_pid < 2) {
                /* keep the tty value */
                /* if we can connect but don't have a pid
                 * then we're not local */

                si->is_local = FALSE;
                si->is_local_is_set = TRUE;

                /* FIXME: get the remote hostname */

                return;
        }

        error = NULL;
        res = ck_process_stat_new_for_unix_pid (xorg_pid, &xorg_stat, &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("stat on pid %d failed: %s", xorg_pid, error->message);
                        g_error_free (error);
                }
                /* keep the tty value */
                return;
        }

        si->x11_display_device = ck_process_stat_get_tty (xorg_stat);
        ck_process_stat_free (xorg_stat);

        si->is_local = TRUE;
        si->is_local_is_set = TRUE;

        g_free (si->remote_host_name);
        si->remote_host_name = NULL;
}

static gboolean
fill_session_info (SessionInfo *si)
{
        CkProcessStat *stat;
        GError        *error;
        gboolean       res;

        error = NULL;
        res = ck_process_stat_new_for_unix_pid (si->pid, &stat, &error);
        if (! res) {
                if (error != NULL) {
                        g_warning ("stat on pid %d failed: %s", si->pid, error->message);
                        g_error_free (error);
                }

                return FALSE;
        }

        si->display_device = ck_process_stat_get_tty (stat);
        ck_process_stat_free (stat);

        fill_x11_info (si);

        if (! si->is_local_is_set) {
                /* FIXME: how should we set this? */
                /* non x11 sessions must be local I guess */
                si->is_local = TRUE;
                si->is_local_is_set = TRUE;
        }

        res = ck_unix_pid_get_login_session_id (si->pid, &si->login_session_id);
        if (! res) {
                si->login_session_id = NULL;
        }

        return TRUE;
}

static void
print_session_info (SessionInfo *si)
{
        printf ("unix-user = %u\n", si->uid);
        if (si->x11_display != NULL) {
                printf ("x11-display = %s\n", si->x11_display);
        }
        if (si->x11_display_device != NULL) {
                printf ("x11-display-device = %s\n", si->x11_display_device);
        }
        if (si->display_device != NULL) {
                printf ("display-device = %s\n", si->display_device);
        }
        if (si->remote_host_name != NULL) {
                printf ("remote-host-name = %s\n", si->remote_host_name);
        }
        if (si->is_local_is_set == TRUE) {
                printf ("is-local = %s\n", si->is_local ? "true" : "false");
        }
        if (si->login_session_id != NULL) {
                printf ("login-session-id = %s\n", si->login_session_id);
        }
}

static gboolean
collect_session_info (uid_t uid,
                      pid_t pid)
{
        SessionInfo *si;
        gboolean     ret;

        si = g_new0 (SessionInfo, 1);

        si->uid = uid;
        si->pid = pid;

        ret = fill_session_info (si);
        if (ret) {
                print_session_info (si);
        }

        session_info_free (si);

        return ret;
}

int
main (int    argc,
      char **argv)
{
        GOptionContext     *context;
        gboolean            ret;
        GError             *error;
        static int          user_id = -1;
        static int          process_id = -1;
        static GOptionEntry entries [] = {
                { "uid", 0, 0, G_OPTION_ARG_INT, &user_id, "User ID", NULL },
                { "pid", 0, 0, G_OPTION_ARG_INT, &process_id, "Process ID", NULL },
                { NULL }
        };

        /* For now at least restrict this to root */
        if (getuid () != 0) {
                g_warning ("You must be root to run this program");
                exit (1);
        }

        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, NULL);
        error = NULL;
        ret = g_option_context_parse (context, &argc, &argv, &error);
        g_option_context_free (context);

        if (! ret) {
                g_warning ("%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (user_id < 0) {
                g_warning ("Invalid UID");
                exit (1);
        }

        if (process_id < 2) {
                g_warning ("Invalid PID");
                exit (1);
        }

        ret = collect_session_info (user_id, process_id);

	return ret != TRUE;
}
