/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#ifdef HAVE_SYS_VT_H
#include <sys/vt.h>
#endif

#define DEV_ENCODE(M,m) ( \
  ( (M&0xfff) << 8)   |   ( (m&0xfff00) << 12)   |   (m&0xff)   \
)

#include <sys/int_types.h>
#include <sys/mkdev.h>
#define _STRUCTURED_PROC 1
#include <sys/procfs.h>
#define NO_TTY_VALUE DEV_ENCODE(-1,-1)

#include "ck-sysdeps.h"

#ifndef ERROR
#define ERROR -1
#endif

/* adapted from procps */
struct _CkProcessStat
{
        int pid;
        int ppid;                       /* stat,status     pid of parent process */
        char state;                     /* stat,status     single-char code for process state (S=sleeping) */
        char cmd[16];                   /* stat,status     basename of executable file in call to exec(2) */
        unsigned long long utime;       /* stat            user-mode CPU time accumulated by process */
        unsigned long long stime;       /* stat            kernel-mode CPU time accumulated by process */
        unsigned long long cutime;      /* stat            cumulative utime of process and reaped children */
        unsigned long long cstime;      /* stat            cumulative stime of process and reaped children */
        unsigned long long start_time;  /* stat            start time of process -- seconds since 1-1-70 */
        unsigned long start_code;       /* stat            address of beginning of code segment */
        unsigned long end_code;         /* stat            address of end of code segment */
        unsigned long start_stack;      /* stat            address of the bottom of stack for the process */
        unsigned long kstk_esp;         /* stat            kernel stack pointer */
        unsigned long kstk_eip;         /* stat            kernel instruction pointer */
        unsigned long wchan;            /* stat (special)  address of kernel wait channel proc is sleeping in */
        long priority;                  /* stat            kernel scheduling priority */
        long nice;                      /* stat            standard unix nice level of process */
        long rss;                       /* stat            resident set size from /proc/#/stat (pages) */
        long alarm;                     /* stat            ? */
        unsigned long rtprio;           /* stat            real-time priority */
        unsigned long sched;            /* stat            scheduling class */
        unsigned long vsize;            /* stat            number of pages of virtual memory ... */
        unsigned long rss_rlim;         /* stat            resident set size limit? */
        unsigned long flags;            /* stat            kernel flags for the process */
        unsigned long min_flt;          /* stat            number of minor page faults since process start */
        unsigned long maj_flt;          /* stat            number of major page faults since process start */
        unsigned long cmin_flt;         /* stat            cumulative min_flt of process and child processes */
        unsigned long cmaj_flt;         /* stat            cumulative maj_flt of process and child processes */
        int     pgrp;                   /* stat            process group id */
        int session;                    /* stat            session id */
        int nlwp;                       /* stat    number of threads, or 0 if no clue */
        int tty;                        /* stat            full device number of controlling terminal */
        int tpgid;                      /* stat            terminal process group id */
        int exit_signal;                /* stat            might not be SIGCHLD */
        int processor;                  /* stat            current (or most recent?) CPU */
        uintptr_t penv;                 /* stat            address of initial environment vector */
        char tty_text[16];              /* stat            device name */

};

pid_t
ck_process_stat_get_ppid (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, -1);

        return stat->ppid;
}

char *
ck_process_stat_get_cmd (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, NULL);

        return g_strdup (stat->cmd);
}

/* adapted from procps */
char *
ck_process_stat_get_tty (CkProcessStat *stat)
{
        guint dev;
        char *tty;
        guint dev_maj;
        guint dev_min;
        pid_t pid;

        g_return_val_if_fail (stat != NULL, NULL);

        return g_strdup (stat->tty_text);
}

/* return 1 if it works, or 0 for failure */
static gboolean
stat2proc (pid_t        pid,
           CkProcessStat *P)
{
        struct psinfo p;
        char          buf[32];
        int           num;
        int           fd;
        int           tty_maj;
        int           tty_min;

        snprintf (buf, sizeof buf, "/proc/%d/psinfo", pid);

        if ((fd = open (buf, O_RDONLY, 0) ) == -1 ) {
                return FALSE;
        }

        num = read (fd, &p, sizeof p);
        close (fd);

        if (num != sizeof p) {
                return FALSE;
        }

        num = PRFNSZ;
        if (num >= sizeof P->cmd) {
                num = sizeof P->cmd - 1;
        }

        memcpy (P->cmd, p.pr_fname, num);  /* p.pr_fname or p.pr_lwp.pr_name */

        P->cmd[num]   = '\0';
        P->pid        = p.pr_pid;
        P->ppid       = p.pr_ppid;
        P->pgrp       = p.pr_pgid;
        P->session    = p.pr_sid;
        P->rss        = p.pr_rssize;
        P->vsize      = p.pr_size;
        P->start_time = p.pr_start.tv_sec;
        P->wchan      = p.pr_lwp.pr_wchan;
        P->state      = p.pr_lwp.pr_sname;
        P->nice       = p.pr_lwp.pr_nice;
        P->priority   = p.pr_lwp.pr_pri;  /* or pr_oldpri */
        P->penv       = p.pr_envp;

        /* we like it Linux-encoded :-) */
        tty_maj = major (p.pr_ttydev);
        tty_min = minor (p.pr_ttydev);
        P->tty = DEV_ENCODE (tty_maj,tty_min);

        snprintf (P->tty_text, sizeof P->tty_text, "%3d,%-3d", tty_maj, tty_min);

	if (tty_maj == 15) {
		snprintf (P->tty_text, sizeof P->tty_text, "/dev/vt/%u", tty_min);
        }
        if (tty_maj == 24) {
                snprintf (P->tty_text, sizeof P->tty_text, "/dev/pts/%u", tty_min);
        }

        if (P->tty == NO_TTY_VALUE) {
#ifdef HAVE_SYS_VT_H
                memcpy (P->tty_text, "   ?   ", 8);
#else
                /*
                 * This is a bit of a hack.  On Solaris, pre-VT integration, the
                 * Xorg process is not assigned a TTY.  So, just assign the value
                 * to "/dev/console" if running without VT support.  This will
                 * allow people using Solaris pre-VT integration to use
                 * ConsoleKit.
                 */
                memcpy (P->tty_text, "/dev/console", 12);
#endif
        }

        if (P->tty == DEV_ENCODE(0,0)) {
                memcpy (P->tty_text, "/dev/console", 12);
        }

        if (P->pid != pid) {
                return FALSE;
        }

        return TRUE;
}

gboolean
ck_process_stat_new_for_unix_pid (pid_t           pid,
                                  CkProcessStat **stat,
                                  GError        **error)
{
        char        *path;
        char        *contents;
        gsize        length;
        gboolean     res;
        GError      *local_error;
        CkProcessStat *proc;

        g_return_val_if_fail (pid > 1, FALSE);

        if (stat == NULL) {
                return FALSE;
        }

        proc = g_new0 (CkProcessStat, 1);
        proc->pid = pid;
        res = stat2proc (pid, proc);
        if (res) {
                *stat = proc;
        } else {
                g_propagate_error (error, local_error);
                *stat = NULL;
        }

        return res;
}

void
ck_process_stat_free (CkProcessStat *stat)
{
        g_free (stat);
}

GHashTable *
ck_unix_pid_get_env_hash (pid_t pid)
{
        GHashTable *hash;
        char       *cmd;
        char        buf[BUFSIZ];
        FILE       *fp;
        int         i;

        hash = g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      g_free,
                                      g_free);

        cmd = g_strdup_printf ("pargs -e %d", pid);
        fp = popen (cmd, "r");
        g_free (cmd);

        while (fgets (buf, BUFSIZ, fp) != NULL) {
                g_strchomp (buf);
                if (g_str_has_prefix (buf, "envp[")) {
                        char *skip_prefix;

                        skip_prefix = strstr (buf, " ");

                        if (skip_prefix != NULL) {
                                char **vals;
                                vals = g_strsplit (skip_prefix + 1, "=", 2);
                                if (vals != NULL) {
                                        g_hash_table_insert (hash,
                                                             g_strdup (vals[0]),
                                                             g_strdup (vals[1]));
                                        g_strfreev (vals);
                                }
                        }
                }
        }

 out:
        pclose (fp);
        return hash;
}

char *
ck_unix_pid_get_env (pid_t       pid,
                     const char *var)
{
        GHashTable *hash;
        char       *val;

        /*
	 * Would probably be more efficient to just loop through the
	 * environment and return the value, avoiding building the hash
	 * table, but this works for now.
	 */
        hash = ck_unix_pid_get_env_hash (pid);
        val  = g_strdup (g_hash_table_lookup (hash, var));
        g_hash_table_destroy (hash);

        return val;
}

uid_t
ck_unix_pid_get_uid (pid_t pid)
{
        struct stat st;
        char       *path;
        int         uid;
        int         res;

        g_return_val_if_fail (pid > 1, 0);

        uid = -1;

        path = g_strdup_printf ("/proc/%u", (guint)pid);
        res = stat (path, &st);
        g_free (path);

        if (res == 0) {
                uid = st.st_uid;
        }

        return uid;
}

pid_t
ck_unix_pid_get_ppid (pid_t pid)
{
        int            ppid;
        gboolean       res;
        CkProcessStat *stat;

        g_return_val_if_fail (pid > 1, 0);

        ppid = -1;

        res = ck_process_stat_new_for_unix_pid (pid, &stat, NULL);
        if (! res) {
                goto out;
        }

        ppid = ck_process_stat_get_ppid (stat);

        ck_process_stat_free (stat);

 out:
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
        GError  *error;
        char    *svcprop_stdout;
        int      status;
        int      max_consoles;
        gboolean res;
        gboolean ret;

        ret = FALSE;

        /*
         * On Solaris, the default number of VT's is determined by
         * resources and is stored in the vtdaemon SVC property
         * options/vtnodecount.  If the svcprop command fails, then it can
         * be safely assumed that VT is not supported on this release of
         * Solaris.
         */

        error = NULL;
        svcprop_stdout = NULL;
        status = 0;
        res = g_spawn_command_line_sync ("/usr/bin/svcprop -p options/nodecount vtdaemon",
                                         &svcprop_stdout,
                                         NULL,
                                         &status,
                                         &error);

        if (res) {
                if (error == NULL && svcprop_stdout != NULL) {
                        char *end;

                        end = NULL;
                        errno = 0;
                        max_consoles = strtol (svcprop_stdout, &end, 0);
                        if (end == NULL || end == svcprop_stdout || errno != 0) {
                                max_consoles = 0;
                        } else {
                                ret = TRUE;
                        }
                }
        }

        if (num != NULL) {
                *num = max_consoles;
        }

        g_free (svcprop_stdout);

        return ret;
}

char *
ck_get_console_device_for_num (guint num)
{
        char *device;

        if (num == 1)
                device = g_strdup_printf ("/dev/console", num);
        else
                device = g_strdup_printf ("/dev/vt/%u", num);

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

        if (strcmp (device, "/dev/console") == 0) {
                *num = 1;
        } else if (sscanf (device, "/dev/vt/%u", &n) == 1) {
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
        guint          active;

#ifdef VT_GETSTATE
        struct vt_stat stat;

        g_assert (console_fd != -1);

        active = 0;
        ret = FALSE;

        res = ioctl (console_fd, VT_GETSTATE, &stat);
        if (res == ERROR) {
                perror ("ioctl VT_GETSTATE");
                goto out;
        }

        {
                int i;

                g_debug ("Current VT: tty%d", stat.v_active);
                for (i = 1; i <= 16; i++) {
                        gboolean is_on;
                        is_on = stat.v_state & (1 << i);

                        g_debug ("VT %d:%s", i, is_on ? "on" : "off");
                }
        }

        active = stat.v_active;
        ret = TRUE;

 out:
        if (num != NULL) {
                *num = active;
        }
#else
        /*
         * If not using VT, not really an active number, but return 1,
         * which maps to "/dev/console".
         */
        ret  = TRUE;
        *num = 1;
#endif

        return ret;
}
