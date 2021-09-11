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
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <sys/vt.h>
#include <linux/tty.h>
#include <linux/kd.h>

#include <glib.h>
#include <glib/gstdio.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#ifdef HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif

#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/label.h>
#include <selinux/get_default_type.h>
#include <selinux/context.h>
#endif

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
        char name[32];
        char devfs_type;
} tty_map_node;

static tty_map_node *tty_map = NULL;

#ifdef HAVE_SELINUX
static struct selabel_handle *label_hnd = NULL;
#endif

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
                      "%c " /* state */
                      "%d %d %d %d %d " /* ppid pgrp session tty tpgid */
                      "%lu %lu %lu %lu %lu " /* flags min_flt cmin_flt maj_flt cmaj_flt */
                      "%Lu %Lu %Lu %Lu "  /* utime stime cutime cstime */
                      "%ld %ld " /* priority nice*/
                      "%d " /* nlwp */
                      "%ld " /* alarm */
                      "%Lu "  /* start_time */
                      "%lu " /* vsize */
                      "%ld " /* rss */
                      "%lu %"KLF"u %"KLF"u %"KLF"u %"KLF"u %"KLF"u " /* rss_rlim start_code end_code start_stack kstk_esp kstk_eip */
                      "%*s %*s %*s %*s " /* discard, no RT signals & Linux 2.1 used hex */
                      "%"KLF"u %*u %*u " /* wchan */
                      "%d %d " /* exit_signal processor */
                      "%lu %lu", /* rtprio sched */
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
        gsize       i;
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
                /* This is pretty harmless, usually it means the process
                 * was short lived and we didn't get around to looking at
                 * it before it died. */
                g_debug ("Couldn't read %s: %s", path, error->message);
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
        guint      i;
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
                g_debug ("Couldn't read %s: %s", path, error->message);
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
                g_debug ("Couldn't read %s: %s", path, error->message);
                g_error_free (error);
                goto out;
        }

        if (contents[0] == '\0') {
                g_debug ("Couldn't read %s: empty file", path);
                goto out;
        }

        errno = 0;
        ulong_value = strtoul (contents, &end_of_valid_ulong, 10);

        if (*end_of_valid_ulong != '\0') {
                goto out;
        }

        if (errno == ERANGE) {
                g_debug ("Couldn't read %s: %s", path, g_strerror (errno));
                goto out;
        }

        /* Will be G_MAXUINT32 if it isn't set */
        if (ulong_value == G_MAXUINT32) {
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

/* adapted from upower-0.9 branch */
static gfloat
linux_get_used_swap (void)
{
        gchar *contents = NULL;
        gchar **lines = NULL;
        GError *error = NULL;
        gchar **tokens;
        gboolean ret;
        guint active = 0;
        guint swap_free = 0;
        guint swap_total = 0;
        guint len;
        guint i;
        gfloat percentage = 0.0f;
        const gchar *filename = "/proc/meminfo";

        /* get memory data */
        ret = g_file_get_contents (filename, &contents, NULL, &error);
        if (!ret) {
                g_debug ("failed to open %s: %s", filename, error->message);
                g_error_free (error);
                goto out;
        }

        /* process each line */
        lines = g_strsplit (contents, "\n", -1);
        for (i=1; lines[i] != NULL; i++) {
                tokens = g_strsplit_set (lines[i], ": ", -1);
                len = g_strv_length (tokens);
                if (len > 3) {
                        if (g_strcmp0 (tokens[0], "SwapFree") == 0)
                                swap_free = atoi (tokens[len-2]);
                        if (g_strcmp0 (tokens[0], "SwapTotal") == 0)
                                swap_total = atoi (tokens[len-2]);
                        else if (g_strcmp0 (tokens[0], "Active(anon)") == 0)
                                active = atoi (tokens[len-2]);
                }
                g_strfreev (tokens);
        }

        /* first check if we even have swap, if not consider all swap space used */
        if (swap_total == 0) {
                g_debug ("no swap space found");
                percentage = 100.0f;
                goto out;
        }
        /* work out how close to the line we are */
        if (swap_free > 0 && active > 0)
                percentage = (active * 100) / swap_free;
        g_debug ("total swap available %i kb, active memory %i kb (%.1f%%)", swap_free, active, percentage);
out:
        g_free (contents);
        g_strfreev (lines);
        return percentage;
}

/* adapted from upower-0.9 branch */
static gboolean
linux_check_enough_swap (void)
{
        gfloat waterline = 98.0f; /* 98% */
        gfloat used_swap = linux_get_used_swap();

        if (used_swap < waterline) {
                        g_debug ("enough swap to hibernate");
                        return TRUE;
                } else {
                        g_debug ("not enough swap to hibernate");
                        return FALSE;
                }

        g_debug ("should not hit this in linux_check_enough_swap");
        return FALSE;
}


static gboolean
linux_supports_sleep_state (const gchar *state)
{
        gboolean ret = FALSE;
        gchar *command;
        GError *error = NULL;
        gint exit_status;

        /* run script from pm-utils */
        command = g_strdup_printf ("/usr/bin/pm-is-supported --%s", state);
        g_debug ("excuting command: %s", command);
        ret = g_spawn_command_line_sync (command, NULL, NULL, &exit_status, &error);
        if (!ret) {
                g_debug ("failed to run script: %s", error->message);
                g_error_free (error);
                goto out;
        }
        ret = (WIFEXITED(exit_status) && (WEXITSTATUS(exit_status) == EXIT_SUCCESS));

out:
        g_free (command);

        return ret;
}

gboolean
ck_system_can_suspend (void)
{
        return linux_supports_sleep_state ("suspend");
}

gboolean
ck_system_can_hibernate (void)
{
        if (linux_supports_sleep_state ("hibernate"))
		return linux_check_enough_swap() ;
        return FALSE;
}

gboolean
ck_system_can_hybrid_sleep (void)
{
        return linux_supports_sleep_state ("suspend-hybrid");
}

static gboolean
ck_selinux_open(void)
{
#ifdef HAVE_SELINUX
        TRACE ();

        if (label_hnd)
                return TRUE;

        if (is_selinux_enabled() <= 0)
                return TRUE;

        label_hnd = selabel_open(SELABEL_CTX_FILE, NULL, 0);
        if (label_hnd) {
                return TRUE;
        } else {
                g_info ("Failed to open selabel handle, reason was: %s", strerror(errno));
                errno = 0;

                // do not fail in permissive mode
                return (security_getenforce() == 1) ? FALSE : TRUE;
        }
#endif

        return TRUE;
}

static void
ck_selinux_close(void)
{
#ifdef HAVE_SELINUX
        if (label_hnd) {
                selabel_close(label_hnd);
                label_hnd = NULL;
        }
#endif
}

static gchar*
ck_selinux_lookup_context(const gchar *dest)
{
#ifdef HAVE_SELINUX
        int rc;
        GStatBuf st;
        mode_t mode = 0;
        security_context_t con;
        gchar *constr;

        if (!label_hnd)
                return NULL;

        errno = 0;
        memset(&st, 0, sizeof(st));
        rc = g_lstat(dest, &st);
        if (rc == 0)
                mode = st.st_mode;
        else if (errno != ENOENT)
                return NULL;

        errno = 0;
        rc = selabel_lookup_raw(label_hnd, &con, dest, mode);
        if (rc < 0 && errno != ENOENT) {
                errno = 0;
                return NULL;
        }

        constr = g_strdup(con);
        freecon(con);

        return constr;
#endif

        return NULL;
}

gboolean
ck_make_tmpfs (guint uid, guint gid, const gchar *dest)
{
#ifdef HAVE_SYS_MOUNT_H
        gchar        *opts;
        gchar        *context;
        int           result;

        TRACE ();

        context = ck_selinux_lookup_context(dest);
        if (context) {
                opts = g_strdup_printf ("mode=0700,uid=%d,rootcontext=%s", uid, context);
        } else {
                opts = g_strdup_printf ("mode=0700,uid=%d", uid);
        }

        g_debug ("mounting tmpfs. uid=%d, gid=%d, dest=%s, opts=%s", uid, gid, dest, opts);
        result = mount("none", dest, "tmpfs", 0, opts);

        g_free (context);
        g_free (opts);

        if (result == 0) {
                return TRUE;
        } else {
                g_info ("Failed to create tmpfs mount, reason was: %s", strerror(errno));
                errno = 0;
                return FALSE;
        }
#endif

        return FALSE;
}

gboolean
ck_remove_tmpfs (guint uid, const char* dest)
{
#ifdef HAVE_SYS_MOUNT_H
        int           result;

        TRACE ();

        result = umount2(dest, MNT_DETACH);

        if (result == 0) {
                return TRUE;
        }

        g_info ("Failed to unmount tmpfs mount, reason was: %s", strerror(errno));
        errno = 0;
#endif

        return FALSE;
}

#ifdef HAVE_SYS_VT_SIGNAL
/* For the moment this is Linux only.
 * Returns the vt file descriptor or < 0 on failure.
 * /sys/class/tty/tty0/active on Linux
 */
gint
ck_get_vt_signal_fd (void)
{
        gint fd;
        const char *errmsg = NULL;

        errno = 0;
        /* Open the magic Linux location */
        fd = open ("/sys/class/tty/tty0/active", O_RDONLY);
        if (fd < 0) {
                errmsg = g_strerror (errno);
                g_error ("ck_get_vt_signal_fd: Error opening sys file: %s",
                         errmsg);
        }

        return fd;
}

static gboolean
poll_vt_fd (gint sys_fd)
{
        struct pollfd fds[1];
        const char *errmsg = NULL;

        errno = 0;
        fds[0].fd = sys_fd;
        fds[0].events = POLLPRI;

        for(;;) {
                g_debug ("poll_vt_fd: polling");
                if (poll(fds, 1, -1) < 0) {
                        errmsg = g_strerror (errno);

                        /* Error handling */
                        if (errno == EINTR || errno == EAGAIN) {
                                g_debug ("poll_vt_fd: Interrupted while waiting for vt event: %s",
                                          errmsg);
                                /* try again */
                        } else {
                                g_error ("poll_vt_fd: Error polling for vt event: %s",
                                         errmsg);
                                return FALSE;
                        }
                } else if (fds[0].revents & POLLPRI) {
                        /* There's something we care about! */
                        return TRUE;
                } else {
                        /* Something we don't care about */
                }
        }

        return FALSE;
}

static gint
read_from_vt_fd (gint sys_fd)
{
        gint ret = -1;
        char new_vt[32];
        ssize_t bytes_read = 0;

        new_vt[31] = '\0';

        while (bytes_read == 0) {
                errno = 0;

                bytes_read = read (sys_fd, &new_vt, 30);
                if (bytes_read == -1 && (errno == EAGAIN || errno == EINTR)) {
                        g_debug ("read_from_vt_fd: Interrupted while reading from sys_fd: %s",
                                 g_strerror (errno));

                        bytes_read = 0;
                        /* try again */
                } else if (bytes_read == -1) {
                        g_error ("read_from_vt_fd: Error while reading from sys_fd: %s",
                                 g_strerror (errno));
                        ret = -1;
                } else {
                        gchar unused[32];
                        gint  vty_num;

                        g_debug ("read_from_vt_fd: got %s", new_vt);

                        if(sscanf (new_vt, "%31[a-z-A-Z]%d", unused, &vty_num) == 2) {
                                g_debug ("read_from_vt_fd: parsed as %s %d", unused, vty_num);
                                ret = vty_num;
                        }

                        /* back to the beginning of the fd */
                        if (lseek(sys_fd, 0,SEEK_SET) < 0)
                        {
                                g_debug ("read_from_vt_fd: error seeking to beginning of file %s",
                                         g_strerror (errno));
                                ret = -1;
                        }
                }
        }

        return ret;
}

/*
 * Returns FALSE if something went wrong with reading/polling the
 * vt fd for VT changes.
 */
gboolean
ck_wait_for_console_switch (gint sys_fd, gint32 *num)
{
        gint new_vt = -1;

        g_debug ("ck_wait_for_console_switch: sys_fd opened");

        /* Poll for changes */
        if (poll_vt_fd (sys_fd)) {
                g_debug ("ck_wait_for_console_switch: poll_vt_fd returned");
                /* Read the changes */
                new_vt = read_from_vt_fd (sys_fd);
                if (new_vt >= 0) {
                        g_debug ("ck_wait_for_console_switch: read successful");
                        /* success, update */
                        if (num != NULL) {
                                *num = new_vt;
                        }
                }
        }

        return new_vt < 0 ? FALSE : TRUE;
}
#endif /* HAVE_SYS_VT_SIGNAL */

gboolean
ck_sysdeps_init (void)
{
        return ck_selinux_open();
}

void
ck_sysdeps_fini (void)
{
        ck_selinux_close();
}
