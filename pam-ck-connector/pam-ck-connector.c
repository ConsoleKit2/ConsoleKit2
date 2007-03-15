/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * pam-ck-connector.c : PAM module for registering with CK
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

#include "config.h"

#include <ctype.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#ifndef _PATH_DEV
#define _PATH_DEV "/dev/"
#endif

#define PAM_SM_SESSION

#include <security/pam_modules.h>
#include <security/_pam_macros.h>
#ifdef HAVE_SECURITY_PAM_MODUTIL_H
#include <security/pam_modutil.h>
#endif
#ifdef HAVE_SECURITY_PAM_EXT_H
#include <security/pam_ext.h>
#endif

#include "ck-connector.h"

static int opt_debug = FALSE;

#ifndef HAVE_PAM_SYSLOG

#ifndef LOG_AUTHPRIV
#define LOG_AUTHPRIV LOG_AUTH
#endif

static void
ck_pam_vsyslog (const pam_handle_t *pamh,
                int                 priority,
                const char         *fmt,
                va_list             args)
{
        char        msgbuf1 [1024];
        char        msgbuf2 [1024];
        int         save_errno;
        const char *service;
        const char *mod_name;
        const char *choice;
        int         res;

        save_errno = errno;
        mod_name = "pam_ck_connector";
        choice = "session";

        if (pamh != NULL) {
                res = pam_get_item (pamh, PAM_SERVICE, (void *) &service);
                if (service == NULL || *service == '\0' || res != PAM_SUCCESS) {
                        service = "<unknown>";
                }
        } else {
                service = "<unknown>";
        }

        res = snprintf (msgbuf1,
                        sizeof (msgbuf1),
                        "%s(%s:%s):",
                        mod_name,
                        service,
                        choice);
	if (res < 0) {
                return;
	}

        errno = save_errno;
        res = vsnprintf (msgbuf2, sizeof (msgbuf2), fmt, args);
        if (res < 0) {
                return;
        }

        errno = save_errno;
        syslog (LOG_AUTHPRIV|priority, "%s %s", msgbuf1, msgbuf2);
}

static void
ck_pam_syslog (const pam_handle_t *pamh,
               int                 priority,
               const char         *fmt,
               ...)
{
        va_list args;

        va_start (args, fmt);
        ck_pam_vsyslog (pamh, priority, fmt, args);
        va_end (args);
}
#else
#define ck_pam_syslog(pamh, priority, ...) pam_syslog(pamh, priority, __VA_ARGS__)
#endif

static void
_parse_pam_args (const pam_handle_t *pamh,
                 int                 flags,
                 int                 argc,
                 const char        **argv)
{
        int i;

        opt_debug = FALSE;
        for (i = 0; i < argc && argv[i] != NULL; i++) {
                if (strcmp (argv[i] , "debug") == 0) {
                        opt_debug = TRUE;
                } else {
                        ck_pam_syslog (pamh, LOG_ERR, "unknown option: %s", argv[i]);
                }
        }
}


PAM_EXTERN int
pam_sm_authenticate (pam_handle_t *pamh,
                     int           flags,
                     int           argc,
                     const char  **argv)
{
        return PAM_IGNORE;
}

PAM_EXTERN int
pam_sm_setcred (pam_handle_t *pamh,
                int           flags,
                int           argc,
                const char  **argv)
{
        return PAM_IGNORE;
}

static uid_t
_util_name_to_uid (const char *username,
                   gid_t      *default_gid)
{
        int           rc;
        uid_t         res;
        char         *buf = NULL;
        unsigned int  bufsize;
        struct passwd pwd;
        struct passwd *pwdp;

        res = (uid_t) -1;

        bufsize = sysconf (_SC_GETPW_R_SIZE_MAX);
        buf = calloc (sizeof (char), bufsize);
        rc = getpwnam_r (username, &pwd, buf, bufsize, &pwdp);
        if (rc != 0 || pwdp == NULL) {
                goto out;
        }

        res = pwdp->pw_uid;
        if (default_gid != NULL) {
                *default_gid = pwdp->pw_gid;
        }

out:
        free (buf);
        return res;
}

/* our singleton */
static CkConnector *ckc = NULL;

PAM_EXTERN int
pam_sm_close_session (pam_handle_t *pamh,
                      int           flags,
                      int           argc,
                      const char  **argv)
{
        if (ckc != NULL) {
                ck_connector_unref (ckc);
                ckc = NULL;
        }
        return PAM_SUCCESS;
}

PAM_EXTERN int
pam_sm_open_session (pam_handle_t *pamh,
                     int           flags,
                     int           argc,
                     const char  **argv)
{
        int         ret;
        int         res;
        const char *user;
        const char *display_device;
        const char *x11_display;
        const char *remote_host_name;
        const char *s;
        uid_t       uid;
        char        buf[256];
        char        ttybuf[PATH_MAX];
        DBusError   error;
        dbus_bool_t is_local;

        ret = PAM_IGNORE;

        display_device = "";
        x11_display = "";
        remote_host_name = "";
        is_local = TRUE;

        _parse_pam_args (pamh, flags, argc, argv);

        /* Register with ConsoleKit as part of the session management */
        if (ckc != NULL) {
                ck_pam_syslog (pamh, LOG_ERR, "process already registered with ConsoleKit");
                goto out;
        }

        ckc = ck_connector_new ();
        if (ckc == NULL) {
                ck_pam_syslog (pamh, LOG_ERR, "oom creating ConsoleKit connector object");
                goto out;
        }

        if (pam_get_user (pamh, &user, NULL) != PAM_SUCCESS || user == NULL) {
                ck_pam_syslog (pamh, LOG_ERR, "cannot determine username");
                goto out;
        }

        if (pam_get_item (pamh, PAM_TTY, (const void **) &display_device) != PAM_SUCCESS || display_device == NULL) {
                ck_pam_syslog (pamh, LOG_ERR, "cannot determine display-device");
                goto out;
        }

        /* interpret any tty with a colon as a DISPLAY */
        if (strchr (display_device, ':') != NULL) {
                x11_display = display_device;
                display_device = "";
        } else if (strncmp (_PATH_DEV, display_device, 5) != 0) {
                snprintf (ttybuf, sizeof (ttybuf), _PATH_DEV "%s", display_device);
                display_device = ttybuf;
        }

        if (pam_get_item (pamh, PAM_RHOST, (const void **) &s) == PAM_SUCCESS && s != NULL) {
                remote_host_name = s;
                if (opt_debug) {
                        ck_pam_syslog (pamh, LOG_INFO, "using '%s' as remote-host-name", remote_host_name);
                }
                is_local = FALSE;
        }

        if ((s = pam_getenv (pamh, "CKCON_TTY")) != NULL) {
                display_device = s;
                if (opt_debug) {
                        ck_pam_syslog (pamh, LOG_INFO, "using '%s' as display-device (from CKCON_TTY)", display_device);
                }
        }

        if ((s = pam_getenv (pamh, "CKCON_X11_DISPLAY")) != NULL) {
                x11_display = s;
                if (opt_debug) {
                        ck_pam_syslog (pamh, LOG_INFO, "using '%s' as X11 display (from CKCON_X11_DISPLAY)", x11_display);
                }
        }

        uid = _util_name_to_uid (user, NULL);
        if (uid == (uid_t) -1) {
                ck_pam_syslog (pamh, LOG_ERR, "cannot determine uid for user '%s'", user);
                goto out;
        } else {
                if (opt_debug) {
                        ck_pam_syslog (pamh, LOG_INFO, "using %d as uid", uid);
                }
        }

        dbus_error_init (&error);
        res = ck_connector_open_session_with_parameters (ckc,
                                                         &error,
                                                         "user", &uid,
                                                         "display-device", &display_device,
                                                         "x11-display", &x11_display,
                                                         "remote-host-name", &remote_host_name,
                                                         "is-local", &is_local,
                                                         NULL);
        if (opt_debug) {
                ck_pam_syslog (pamh, LOG_INFO, "open session result: %d", res);
        }

        if (! res) {
                /* this might not be a bug for servers that don't have
                 * the message bus or ConsoleKit daemon running - so
                 * only log a message in debugging mode.
                 */
                if (dbus_error_is_set (&error)) {
                        if (opt_debug) {
                                ck_pam_syslog (pamh, LOG_DEBUG, "%s", error.message);
                        }
                        dbus_error_free (&error);
                } else {
                        if (opt_debug) {
                                ck_pam_syslog (pamh, LOG_DEBUG, "insufficient privileges or D-Bus / ConsoleKit not available");
                        }
                }

                goto out;
        }

        /* now set the cookie */
        buf[sizeof (buf) - 1] = '\0';
        snprintf (buf, sizeof (buf) - 1, "XDG_SESSION_COOKIE=%s", ck_connector_get_cookie (ckc));
        if (pam_putenv (pamh, buf) != PAM_SUCCESS) {
                ck_pam_syslog (pamh, LOG_ERR, "unable to set XDG_SESSION_COOKIE in environment");
                /* tear down session the hard way */
                ck_connector_unref (ckc);
                ckc = NULL;

                goto out;
        }

        if (opt_debug) {
                ck_pam_syslog (pamh, LOG_DEBUG, "registered uid=%d on tty='%s' with ConsoleKit", uid, display_device);
        }

        /* note that we're leaking our CkConnector instance ckc - this
         * is *by design* such that when the login manager (that uses
         * us) exits / crashes / etc. ConsoleKit will notice, via D-Bus
         * connection tracking, that the login session ended.
         */

        ret = PAM_SUCCESS;

out:
        return ret;
}

#ifdef PAM_STATIC

struct pam_module _pam_ckconnector_modstruct = {
        "pam_ck_connector",
        pam_sm_authenticate,
        pam_sm_setcred,
        NULL,
        pam_sm_open_session,
        pam_sm_close_session,
        NULL,
};

#endif

/* end of module definition */
