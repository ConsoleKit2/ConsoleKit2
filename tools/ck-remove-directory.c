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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <ftw.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "ck-sysdeps.h"


static void
become_user (uid_t uid)
{
        int            res;
        struct passwd *pwent;

        errno = 0;
        pwent = getpwuid (uid);
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
        res = setuid (uid);
        if (res == -1) {
                g_warning ("Error performing setuid: %s", g_strerror (errno));
                exit (1);
        }
}

static int
unlink_cb (const char        *fpath,
           const struct stat *sb,
           int                typeflag,
           struct FTW        *ftwbuf)
{
        int ret = remove (fpath);

        if (ret) {
                g_error ("Failed to remove %s, reason was: %s", fpath, strerror(errno));
                errno = 0;
        }

        return ret;
}

static int
remove_dest_dir (const gchar *dest)
{
        return nftw(dest, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

int
main (int    argc,
      char **argv)
{
        GOptionContext     *context;
        gboolean            ret;
        GError             *error;
        static int          user_id = -1;
        static gchar       *dest = NULL;
        static GOptionEntry entries [] = {
                { "uid", 0, 0, G_OPTION_ARG_INT, &user_id, N_("User ID"), NULL },
                { "dest", 0, 0, G_OPTION_ARG_STRING, &dest, N_("Destination to remove"), NULL },
                { NULL }
        };

        /* Setup for i18n */
        setlocale(LC_ALL, "");
 
#ifdef ENABLE_NLS
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);
#endif

        /* For now at least restrict this to root */
        if (getuid () != 0) {
                g_warning (_("You must be root to run this program"));
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

        /* Ensure we have a dest and that it starts with the correct prefix
         * so we don't remove something important.
         */
        if (dest == NULL || !g_str_has_prefix (dest, RUNDIR "/user/")) {
                g_warning ("Invalid Dest");
                exit (1);
        }

        become_user (user_id);

        return remove_dest_dir (dest) == 0 ? 0 : 1;
}
