/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2007 William Jon McCann <mccann@jhu.edu>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/pam_misc.h>

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif /* HAVE_PATHS_H */

#define PAM_MAX_LOGIN_TRIES	3
#define PAM_FAIL_CHECK if (retcode != PAM_SUCCESS) { \
       fprintf (stderr, "\n%s\n", pam_strerror (pamh, retcode)); \
       pam_end (pamh, retcode); exit (1); \
   }
#define PAM_END { \
	pam_setcred (pamh, PAM_DELETE_CRED); \
	retcode = pam_close_session (pamh, 0); \
	pam_end (pamh, retcode); \
}

int
main (int argc, char *argv[])
{
        int             retcode;
        int             ret;
        pam_handle_t   *pamh;
        char           *username;
        char           *hostname;
        char           *tty_name;
        char           *ttyn;
        struct pam_conv conv = { misc_conv, NULL };
        int             failcount;

        ret = 1;
        username = NULL;
        hostname = NULL;
        tty_name = NULL;

        retcode = pam_start ("login", username, &conv, &pamh);
        if (retcode != PAM_SUCCESS) {
                fprintf (stderr, "login: PAM Failure, aborting: %s\n",
                         pam_strerror (pamh, retcode));
                exit (99);
        }

        ttyn = ttyname (0);

        if (strncmp (ttyn, _PATH_DEV, 5) == 0) {
                tty_name = ttyn + 5;
        } else {
                tty_name = ttyn;
        }

        retcode = pam_set_item (pamh, PAM_RHOST, hostname);
        PAM_FAIL_CHECK;
        retcode = pam_set_item (pamh, PAM_TTY, tty_name);
        PAM_FAIL_CHECK;
        pam_set_item (pamh, PAM_USER, NULL);

        retcode = pam_set_item (pamh, PAM_USER_PROMPT, "Username: ");
        PAM_FAIL_CHECK;

        failcount = 0;
        retcode = pam_authenticate (pamh, 0);
        while ((failcount++ < PAM_MAX_LOGIN_TRIES) &&
               ((retcode == PAM_AUTH_ERR) ||
                (retcode == PAM_USER_UNKNOWN) ||
                (retcode == PAM_CRED_INSUFFICIENT) ||
                (retcode == PAM_AUTHINFO_UNAVAIL))) {
                pam_get_item (pamh, PAM_USER, (const void **) &username);

                fprintf (stderr, "Login incorrect\n\n");
                pam_set_item (pamh, PAM_USER, NULL);
                retcode = pam_authenticate (pamh, 0);
        }

        if (retcode != PAM_SUCCESS) {
                fprintf (stderr, "\nLogin incorrect\n");
                pam_end (pamh, retcode);
                exit (0);
        }

        retcode = pam_acct_mgmt (pamh, 0);
        if (retcode == PAM_NEW_AUTHTOK_REQD) {
                retcode = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
        }

        PAM_FAIL_CHECK;

        pam_putenv (pamh, "CKCON_TTY=/dev/tty55");
        pam_putenv (pamh, "CKCON_X11_DISPLAY=:50");

        retcode = pam_open_session (pamh, 0);
        PAM_FAIL_CHECK;

        retcode = pam_setcred (pamh, PAM_ESTABLISH_CRED);
        PAM_FAIL_CHECK;

        pam_get_item (pamh, PAM_USER, (const void **) &username);

        printf ("Session opened for %s\n", username);

        printf ("sleeping for 20 seconds...");
        sleep (20);

        PAM_END;

        printf ("\nSession closed\n");

        return ret;
}
