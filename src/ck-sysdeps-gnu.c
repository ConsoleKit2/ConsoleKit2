/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Pino Toscano <pino@kde.org>
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
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <hurd.h>
#include <dirent.h>
#include <ps.h>
#include <ttyent.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#include "ck-sysdeps.h"

struct _CkProcessStat
{
        struct proc_stat *ps;           /* the statistics of a process */
};

static struct ps_context *pc = NULL;

static gboolean
get_proc_stat_from_pid (pid_t              pid,
                        ps_flags_t         flags,
                        struct proc_stat **res_ps)
{
        error_t           err;
        struct proc_stat *ps;

        g_assert (pid >= 0);
        g_assert (res_ps != NULL);

        if (pc == NULL) {
                err = ps_context_create (getproc (), &pc);
                if (err) {
                    return FALSE;
                }
        }

        err = _proc_stat_create (pid, pc, &ps);
        if (err) {
                return FALSE;
        }

        err = proc_stat_set_flags (ps, PSTAT_PID | flags);
        if (err) {
                return FALSE;
        }

        *res_ps = ps;
        return TRUE;
}


pid_t
ck_process_stat_get_ppid (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, -1);

        return proc_stat_pid (stat->ps);
}

char *
ck_process_stat_get_cmd (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, NULL);

        return g_strdup (proc_stat_args (stat->ps));
}

char *
ck_process_stat_get_tty (CkProcessStat *stat)
{
        struct ps_tty    *tty;

        g_return_val_if_fail (stat != NULL, NULL);

        tty = proc_stat_tty (stat->ps);

        return tty ? g_strdup (ps_tty_name (tty)) : NULL;
}

gboolean
ck_process_stat_new_for_unix_pid (pid_t           pid,
                                  CkProcessStat **stat,
                                  GError        **error)
{
        gboolean          res;
        struct proc_stat *ps;
        CkProcessStat    *proc;

        g_return_val_if_fail (pid > 1, FALSE);

        if (stat == NULL) {
                return FALSE;
        }

        *stat = NULL;

        res = get_proc_stat_from_pid (pid, PSTAT_ARGS | PSTAT_TTY, &ps);
        if (!res) {
                return FALSE;
        }

        proc = g_new0 (CkProcessStat, 1);
        proc->ps = ps;
        *stat = proc;

        return TRUE;
}

void
ck_process_stat_free (CkProcessStat *stat)
{
        _proc_stat_free (stat->ps);

        g_free (stat);
}

GHashTable *
ck_unix_pid_get_env_hash (pid_t pid)
{
        struct proc_stat *ps;
        char             *env_p;
        size_t            env_index;
        size_t            env_l;
        gboolean          res;
        GHashTable       *hash;

        g_return_val_if_fail (pid > 1, NULL);

        res = get_proc_stat_from_pid (pid, PSTAT_ENV, &ps);
        if (!res) {
                return NULL;
        }

        hash = g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      g_free,
                                      g_free);

        env_index = 0;
        env_l = 0;
        env_p = proc_stat_env (ps);
        while (env_index < proc_stat_env_len (ps)) {
                env_l = strlen (env_p);
                env_index += env_l + 1;
                if (env_l) {
                        char **vals;
                        vals = g_strsplit (env_p, "=", 2);
                        if (vals != NULL) {
                                g_hash_table_insert (hash,
                                                     g_strdup (vals[0]),
                                                     g_strdup (vals[1]));
                                g_strfreev (vals);
                        }
                }
                env_p = env_p + env_l + 1;
        }

        _proc_stat_free (ps);

        return hash;
}

char *
ck_unix_pid_get_env (pid_t       pid,
                     const char *var)
{
        struct proc_stat *ps;
        char             *env_p;
        size_t            env_index;
        size_t            env_l;
        char             *prefix;
        int               prefix_len;
        char             *val;
        gboolean          res;

        g_return_val_if_fail (pid > 1, NULL);

        val = NULL;

        res = get_proc_stat_from_pid (pid, PSTAT_ENV, &ps);
        if (!res) {
                return NULL;
        }

        prefix = g_strdup_printf ("%s=", var);
        prefix_len = strlen (prefix);

        env_index = 0;
        env_l = 0;
        env_p = proc_stat_env (ps);
        while (env_index < proc_stat_env_len (ps)) {
                env_l = strlen (env_p);
                env_index += env_l + 1;
                if (env_l && g_str_has_prefix (env_p, prefix)) {
                        val = g_strdup (env_p + prefix_len);
                        break;
                }
                env_p = env_p + env_l + 1;
        }

        g_free (prefix);

        _proc_stat_free (ps);

        return val;
}

uid_t
ck_unix_pid_get_uid (pid_t pid)
{
        struct proc_stat *ps;
        gboolean          res;
        uid_t             uid;

        g_return_val_if_fail (pid > 1, 0);

        res = get_proc_stat_from_pid (pid, PSTAT_OWNER_UID, &ps);
        if (!res) {
                return 0;
        }

        uid = proc_stat_owner_uid (ps);

        _proc_stat_free (ps);

        return uid;
}

pid_t
ck_unix_pid_get_ppid (pid_t pid)
{
        struct proc_stat *ps;
        gboolean          res;
        pid_t             ppid;

        g_return_val_if_fail (pid > 1, 0);

        res = get_proc_stat_from_pid (pid, PSTAT_PROC_INFO, &ps);
        if (!res) {
                return 0;
        }

        ppid = proc_stat_proc_info (ps)->ppid;

        _proc_stat_free (ps);

        return ppid;
}

gboolean
ck_unix_pid_get_login_session_id (pid_t  pid,
                                  char **idp)
{
        g_return_val_if_fail (pid > 1, FALSE);

        return FALSE;
}

gboolean
ck_get_max_num_consoles (guint *num)
{
        int      max_consoles;
        int      res;
        gboolean ret;
        struct ttyent *t;

        ret = FALSE;
        max_consoles = 0;

        res = setttyent ();
        if (res == 0) {
                goto done;
        }

        while ((t = getttyent ()) != NULL) {
                if (t->ty_status & TTY_ON && strncmp (t->ty_name, "tty", 3) == 0)
                        max_consoles++;
        }

        /* Increment one more so that all consoles are properly counted
         * this is arguable a bug in vt_add_watches().
         */
        max_consoles++;

        ret = TRUE;

        endttyent ();

done:
        if (num != NULL) {
                *num = max_consoles;
        }

        return ret;
}

gboolean
ck_supports_activatable_consoles (void)
{
        return TRUE;
}

char *
ck_get_console_device_for_num (guint num)
{
        char *device;

        device = g_strdup_printf (_PATH_TTY "%u", num);

        return device;
}

gboolean
ck_get_console_num_from_device (const char *device,
                                guint      *num)
{
        guint    n;
        gboolean ret;

        n = 0;
        ret = FALSE;

        if (device == NULL) {
                return FALSE;
        }

        if (sscanf (device, _PATH_TTY "%u", &n) == 1) {
                ret = TRUE;
        }

        if (num != NULL) {
                *num = n;
        }

        return ret;
}

gboolean
ck_get_active_console_num (int    console_fd,
                           guint *num)
{
        gboolean       ret;
        int            res;
        long           cur_active;
        char           buf[30];
        guint          active;

        g_assert (console_fd != -1);

        active = 0;
        ret = FALSE;

        res = readlink ("/dev/cons/vcs", buf, sizeof (buf));
        if (res > 0) {
                /* the resolved path is like "/dev/vcs/$number", so skip
                   the non-number part at the start */
                const char *p = buf;
                while ((*p) && ((*p < '0') || (*p > '9'))) {
                        ++p;
                }
                if (*p) {
                        cur_active = strtol (p, NULL, 10);
                        g_debug ("Current VT: tty%ld", cur_active);
                        active = cur_active;
                        ret = TRUE;
                }
        }

        if (num != NULL) {
                *num = active;
        }

        return ret;
}
