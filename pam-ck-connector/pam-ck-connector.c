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

#define PAM_SM_SESSION

#include <security/pam_modules.h>
#include <security/_pam_macros.h>
#include <security/pam_modutil.h>
#include <security/pam_ext.h>

#include "ck-connector.h"

static int opt_debug = FALSE;

static void
_parse_pam_args (const pam_handle_t *pamh,
                 int                 flags,
                 int                 argc,
                 const char        **argv)
{
        int i;

        opt_debug = FALSE;
        for (i = 0; i < argc && argv[i] != NULL; i++) {
                if (strcmp (argv[i] ,"debug") == 0) {
                        opt_debug = TRUE;
                } else {
                        pam_syslog (pamh, LOG_ERR, "unknown option: %s", argv[i]);
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
static CKConnector *ckc = NULL;

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
        const char *user;
        const char *tty;
        const char *x11_display;
        const char *s;
        uid_t       uid;
        char        buf[256];
        DBusError   error;

        ret = PAM_IGNORE;

        _parse_pam_args (pamh, flags, argc, argv);

        /* Register with ConsoleKit as part of the session management */
        if (ckc != NULL) {
                pam_syslog (pamh, LOG_ERR, "process already registered with ConsoleKit");
                goto out;
        }

        ckc = ck_connector_new ();
        if (ckc == NULL) {
                pam_syslog (pamh, LOG_ERR, "oom creating ConsoleKit connector object");
                goto out;
        }

        if (pam_get_user (pamh, &user, NULL) != PAM_SUCCESS || user == NULL) {
                pam_syslog (pamh, LOG_ERR, "cannot determine username");
                goto out;
        }

        if (pam_get_item (pamh, PAM_TTY, (const void **) &tty) != PAM_SUCCESS || tty == NULL) {
                pam_syslog (pamh, LOG_ERR, "cannot determine tty");
                goto out;
        }

        if ((s = pam_getenv (pamh, "CKCON_TTY")) != NULL) {
                tty = s;
                if (opt_debug) {
                        pam_syslog (pamh, LOG_INFO, "using '%s' as tty (from CKCON_TTY)", tty);
                }
        }

        x11_display = NULL;
        if ((s = pam_getenv (pamh, "CKCON_X11_DISPLAY")) != NULL) {
                x11_display = s;
                if (opt_debug)
                        pam_syslog (pamh, LOG_INFO, "using '%s' as X11 display (from CKCON_X11_DISPLAY)", x11_display);
        }

        uid = _util_name_to_uid (user, NULL);
        if (uid == (uid_t) -1) {
                pam_syslog (pamh, LOG_ERR, "cannot determine uid for user '%s'", user);
                goto out;
        }

        dbus_error_init (&error);
        if (! ck_connector_open_session_for_user (ckc, uid, tty, x11_display, &error)) {
                /* this might not be a bug for servers that don't have
                 * the message bus or ConsoleKit daemon running - so
                 * only log a message in debugging mode.
                 */
                if (dbus_error_is_set (&error)) {
                        if (opt_debug) {
                                pam_syslog (pamh, LOG_DEBUG, "%s", error.message);
                        }
                        dbus_error_free (&error);
                } else {
                        if (opt_debug) {
                                pam_syslog (pamh, LOG_DEBUG, "insufficient privileges or
 D-Bus / ConsoleKit not available");
                        }
                }

                goto out;
        }

        /* now set the cookie */
        buf[sizeof (buf) - 1] = '\0';
        snprintf (buf, sizeof (buf) - 1, "XDG_SESSION_COOKIE=%s", ck_connector_get_cookie (ckc));
        if (pam_putenv (pamh, buf) != PAM_SUCCESS) {
                pam_syslog (pamh, LOG_ERR, "unable to set XDG_SESSION_COOKIE vairable");
                /* tear down session the hard way */
                ck_connector_unref (ckc);
                ckc = NULL;
                goto out;
        }

        if (opt_debug) {
                pam_syslog (pamh, LOG_DEBUG, "registered uid=%d on tty='%s' with ConsoleKit", uid, tty);
        }

        /* note that we're leaking our CKConnector instance ckc - this
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
