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

#include <sys/vt.h>
#include <linux/tty.h>
#include <linux/kd.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

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
};

/* adapted from procps */
#define MAJOR_OF(d) ( ((unsigned)(d)>>8u) & 0xfffu )
#define MINOR_OF(d) ( ((unsigned)(d)&0xffu) | (((unsigned)(d)&0xfff00000u)>>12u) )

typedef struct tty_map_node {
        struct tty_map_node *next;
        guint major_number;
        guint minor_first;
        guint minor_last;
        char name[16];
        char devfs_type;
} tty_map_node;

static tty_map_node *tty_map = NULL;

/* adapted from procps */
/* Load /proc/tty/drivers for device name mapping use. */
static void
load_drivers (void)
{
        char buf[10000];
        char *p;
        int fd;
        int bytes;

        fd = open ("/proc/tty/drivers", O_RDONLY);
        if (fd == -1) {
                goto fail;
        }

        bytes = read (fd, buf, sizeof (buf) - 1);
        if (bytes == -1) {
                goto fail;
        }

        buf[bytes] = '\0';
        p = buf;
        while ((p = strstr (p, " " _PATH_DEV))) {
                tty_map_node *tmn;
                int len;
                char *end;

                p += 6;
                end = strchr (p, ' ');
                if (! end) {
                        continue;
                }
                len = end - p;
                tmn = calloc (1, sizeof (tty_map_node));
                tmn->next = tty_map;
                tty_map = tmn;
                /* if we have a devfs type name such as /dev/tts/%d then strip the %d but
                   keep a flag. */
                if (len >= 3 && !strncmp (end - 2, "%d", 2)) {
                        len -= 2;
                        tmn->devfs_type = 1;
                }
                strncpy (tmn->name, p, len);
                p = end; /* set p to point past the %d as well if there is one */
                while (*p == ' ') {
                        p++;
                }

                tmn->major_number = atoi (p);
                p += strspn (p, "0123456789");
                while (*p == ' ') {
                        p++;
                }
                switch (sscanf (p, "%u-%u", &tmn->minor_first, &tmn->minor_last)) {
                default:
                        /* Can't finish parsing this line so we remove it from the list */
                        tty_map = tty_map->next;
                        free (tmn);
                        break;
                case 1:
                        tmn->minor_last = tmn->minor_first;
                        break;
                case 2:
                        break;
                }
        }
 fail:
        if (fd != -1) {
                close (fd);
        }
        if(! tty_map) {
                tty_map = (tty_map_node *)-1;
        }
}

/* adapted from procps */
/* Try to guess the device name from /proc/tty/drivers info. */
static char *
driver_name (guint maj,
             guint min)
{
        struct stat   sbuf;
        tty_map_node *tmn;
        char         *tty;

        if (! tty_map) {
                load_drivers ();
        }
        if (tty_map == (tty_map_node *) - 1) {
                return 0;
        }

        tmn = tty_map;
        for (;;) {
                if (! tmn) {
                        return 0;
                }
                if (tmn->major_number == maj && tmn->minor_first <= min && tmn->minor_last >= min) {
                        break;
                }
                tmn = tmn->next;
        }

        tty = g_strdup_printf (_PATH_DEV "%s%d", tmn->name, min);  /* like "/dev/ttyZZ255" */
        if (stat (tty, &sbuf) < 0){
                g_free (tty);

                if (tmn->devfs_type) {
                        return NULL;
                }

                tty = g_strdup_printf (_PATH_DEV "%s", tmn->name);  /* like "/dev/ttyZZ255" */

                if (stat (tty, &sbuf) < 0) {
                        g_free (tty);
                        return NULL;
                }
        }

        if (min != MINOR_OF (sbuf.st_rdev)) {
                g_free (tty);
                return NULL;
        }

        if (maj != MAJOR_OF (sbuf.st_rdev)) {
                g_free (tty);
                return NULL;
        }

        return tty;
}

/* adapted from procps */
static char *
link_name (guint       maj,
           guint       min,
           int         pid,
           const char *name)
{
        struct stat sbuf;
        char       *path;
        char       *tty;

        path = g_strdup_printf ("/proc/%d/%s", pid, name);
        tty = g_file_read_link (path, NULL);
        g_free (path);

        if (tty == NULL) {
                goto out;
        }

        if (stat (tty, &sbuf) < 0) {
                g_free (tty);
                tty = NULL;
                goto out;
        }

        if (min != MINOR_OF (sbuf.st_rdev)) {
                g_free (tty);
                tty = NULL;
                goto out;

        }
        if (maj != MAJOR_OF (sbuf.st_rdev)) {
                g_free (tty);
                tty = NULL;
                goto out;
        }

 out:
        return tty;
}

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

        pid = stat->pid;
        dev = stat->tty;

        if (dev == 0u) {
                return NULL;
        }

        dev_maj = MAJOR_OF (dev);
        dev_min = MINOR_OF (dev);

        tty = link_name (dev_maj, dev_min, pid, "tty");
        if (tty != NULL) {
                goto out;
        }

        tty = driver_name (dev_maj, dev_min);
        if (tty != NULL) {
                goto out;
        }

        tty = link_name (dev_maj, dev_min, pid, "fd/2");
        if (tty != NULL) {
                goto out;
        }

        tty = link_name (dev_maj, dev_min, pid, "fd/255");
        if (tty != NULL) {
                goto out;
        }

 out:

        return tty;
}

#define KLF "l"
/* adapted from procps */
static void
stat2proc (const char    *S,
           CkProcessStat *P)
{
        unsigned num;
        char   * tmp;

        /* fill in default values for older kernels */
        P->processor = 0;
        P->rtprio = -1;
        P->sched = -1;
        P->nlwp = 0;

        S = strchr (S, '(') + 1;
        tmp = strrchr (S, ')');
        num = tmp - S;
        if (G_UNLIKELY (num >= sizeof P->cmd)) {
                num = sizeof P->cmd - 1;
        }

        memcpy (P->cmd, S, num);
        P->cmd[num] = '\0';
        S = tmp + 2;                 /* skip ") " */

        num = sscanf (S,
                      "%c "
                      "%d %d %d %d %d "
                      "%lu %lu %lu %lu %lu "
                      "%Lu %Lu %Lu %Lu "  /* utime stime cutime cstime */
                      "%ld %ld "
                      "%d "
                      "%ld "
                      "%Lu "  /* start_time */
                      "%lu "
                      "%ld "
                      "%lu %"KLF"u %"KLF"u %"KLF"u %"KLF"u %"KLF"u "
                      "%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
                      "%"KLF"u %*lu %*lu "
                      "%d %d "
                      "%lu %lu",
                      &P->state,
                      &P->ppid, &P->pgrp, &P->session, &P->tty, &P->tpgid,
                      &P->flags, &P->min_flt, &P->cmin_flt, &P->maj_flt, &P->cmaj_flt,
                      &P->utime, &P->stime, &P->cutime, &P->cstime,
                      &P->priority, &P->nice,
                      &P->nlwp,
                      &P->alarm,
                      &P->start_time,
                      &P->vsize,
                      &P->rss,
                      &P->rss_rlim, &P->start_code, &P->end_code, &P->start_stack, &P->kstk_esp, &P->kstk_eip,
                      /*     P->signal, P->blocked, P->sigignore, P->sigcatch,   */ /* can't use */
                      &P->wchan, /* &P->nswap, &P->cnswap, */  /* nswap and cnswap dead for 2.4.xx and up */
                      /* -- Linux 2.0.35 ends here -- */
                      &P->exit_signal, &P->processor,  /* 2.2.1 ends with "exit_signal" */
                      /* -- Linux 2.2.8 to 2.5.17 end here -- */
                      &P->rtprio, &P->sched  /* both added to 2.5.18 */
                      );

        if (!P->nlwp){
                P->nlwp = 1;
        }
}

gboolean
ck_process_stat_new_for_unix_pid (pid_t           pid,
                                  CkProcessStat **stat,
                                  GError        **error)
{
        char          *path;
        char          *contents;
        gsize          length;
        gboolean       res;
        GError        *local_error;
        CkProcessStat *proc;

        g_return_val_if_fail (pid > 1, FALSE);

        if (stat == NULL) {
                return FALSE;
        }

        path = g_strdup_printf ("/proc/%d/stat", pid);

        contents = NULL;
        local_error = NULL;
        res = g_file_get_contents (path,
                                   &contents,
                                   &length,
                                   &local_error);
        if (res) {
                proc = g_new0 (CkProcessStat, 1);
                proc->pid = pid;
                stat2proc (contents, proc);
                *stat = proc;
        } else {
                g_propagate_error (error, local_error);
                *stat = NULL;
        }

        g_free (contents);
        g_free (path);

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
        char       *path;
        gboolean    res;
        char       *contents;
        gsize       length;
        GError     *error;
        GHashTable *hash;
        int         i;
        gboolean    last_was_null;

        g_return_val_if_fail (pid > 1, NULL);

        contents = NULL;
        hash = NULL;

        path = g_strdup_printf ("/proc/%u/environ", (guint)pid);

        error = NULL;
        res = g_file_get_contents (path,
                                   &contents,
                                   &length,
                                   &error);
        if (! res) {
                g_warning ("Couldn't read %s: %s", path, error->message);
                g_error_free (error);
                goto out;
        }

        hash = g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      g_free,
                                      g_free);

        last_was_null = TRUE;
        for (i = 0; i < length; i++) {
                if (contents[i] == '\0') {
                        last_was_null = TRUE;
                        continue;
                }
                if (last_was_null) {
                        char **vals;
                        vals = g_strsplit (contents + i, "=", 2);
                        if (vals != NULL) {
                                g_hash_table_insert (hash,
                                                     g_strdup (vals[0]),
                                                     g_strdup (vals[1]));
                                g_strfreev (vals);
                        }
                }
                last_was_null = FALSE;
        }

 out:
        g_free (contents);
        g_free (path);

        return hash;
}

char *
ck_unix_pid_get_env (pid_t       pid,
                     const char *var)
{
        char      *path;
        gboolean   res;
        char      *contents;
        char      *val;
        gsize      length;
        GError    *error;
        int        i;
        char      *prefix;
        int        prefix_len;
        gboolean   last_was_null;

        g_return_val_if_fail (pid > 1, NULL);

        val = NULL;
        contents = NULL;
        prefix = NULL;

        path = g_strdup_printf ("/proc/%u/environ", (guint)pid);

        error = NULL;
        res = g_file_get_contents (path,
                                   &contents,
                                   &length,
                                   &error);
        if (! res) {
                g_warning ("Couldn't read %s: %s", path, error->message);
                g_error_free (error);
                goto out;
        }


        prefix = g_strdup_printf ("%s=", var);
        prefix_len = strlen (prefix);

        /* FIXME: make more robust */
        last_was_null = TRUE;
        for (i = 0; i < length; i++) {
                if (contents[i] == '\0') {
                        last_was_null = TRUE;
                        continue;
                }
                if (last_was_null && g_str_has_prefix (contents + i, prefix)) {
                        val = g_strdup (contents + i + prefix_len);
                        break;
                }
                last_was_null = FALSE;
        }

 out:
        g_free (prefix);
        g_free (contents);
        g_free (path);

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
        gboolean ret;
        gboolean res;
        char    *path;
        char    *contents;
        gsize    length;
        GError  *error;
        char    *end_of_valid_ulong;
        gulong   ulong_value;

        g_return_val_if_fail (pid > 1, FALSE);

        ret = FALSE;
        contents = NULL;

        path = g_strdup_printf ("/proc/%u/sessionid", (guint)pid);

        error = NULL;
        res = g_file_get_contents (path,
                                   &contents,
                                   &length,
                                   &error);
        if (! res) {
                g_warning ("Couldn't read %s: %s", path, error->message);
                g_error_free (error);
                goto out;
        }

        if (contents[0] == '\0') {
                g_warning ("Couldn't read %s: empty file", path);
                goto out;
        }

        errno = 0;
        ulong_value = strtoul (contents, &end_of_valid_ulong, 10);

        if (*end_of_valid_ulong != '\0') {
                goto out;
        }

        if (errno == ERANGE) {
                g_warning ("Couldn't read %s: %s", path, g_strerror (errno));
                goto out;
        }

        /* Will be G_MAXULONG if it isn't set */
        if (ulong_value == G_MAXULONG) {
                goto out;
        }

        if (idp != NULL) {
                *idp = g_strdup_printf ("%lu", (unsigned long int)ulong_value);
        }

        ret = TRUE;

 out:
        g_free (contents);
        g_free (path);

        return ret;
}

gboolean
ck_get_max_num_consoles (guint *num)
{
        if (num != NULL) {
                *num = MAX_NR_CONSOLES;
        }

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
        guint          active;
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

        return ret;
}
