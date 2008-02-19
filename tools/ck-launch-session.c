/*
 * Copyright Red Hat, Inc. 2007-2008.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Red Hat, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Gate a process inside of a ConsoleKit session.
 *
 */

#include <sys/types.h>
#include <sys/wait.h>
#ifdef HAVE_PATHS_H
#include <paths.h>
#else
#define _PATH_BSHELL "/bin/sh"
#endif

#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "ck-connector.h"

int
main (int argc, char **argv)
{
        CkConnector *ckc = NULL;
        DBusError    error;
        const char  *shell;
        pid_t        pid;
        int          status;

        ckc = ck_connector_new ();
        if (ckc != NULL) {
                dbus_error_init (&error);
                if (ck_connector_open_session (ckc, &error)) {
                        pid = fork ();
                        switch (pid) {
                        case -1:
                                syslog (LOG_ERR, "error forking child");
                                break;
                        case 0:
                                setenv ("XDG_SESSION_COOKIE",
                                       ck_connector_get_cookie (ckc), 1);
                                break;
                        default:
                                waitpid (pid, &status, 0);
                                exit (status);
                                break;
                        }
                } else {
                        syslog (LOG_ERR, "error connecting to ConsoleKit");
                }
        } else {
                syslog (LOG_ERR, "error setting up to connection to ConsoleKit");
        }

        if (argc > 1) {
                execvp (argv[1], argv + 1);
        } else {
                shell = getenv ("SHELL");
                if (shell == NULL) {
                        shell = _PATH_BSHELL;
                }
                execlp (shell, shell, NULL);
        }
        _exit (1);
}
