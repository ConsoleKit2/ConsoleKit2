/*
 * ck-connector.h : Code for login managers to register with ConsoleKit.
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

#ifndef CK_CONNECTOR_H
#define CK_CONNECTOR_H

#include <sys/types.h>
#include <dbus/dbus.h>

struct CKConnecter_s;
typedef struct CKConnector_s CKConnector;

/* Allocates a new CKConnector instance.
 *
 * Returns NULL on OOM */
CKConnector  *ckc_new                  (void);

/* Connects to the D-Bus system bus daemon and issues the method call
 * OpenSessionWithParameters on the ConsoleKit manager interface. The
 * connection to the bus is private. 
 *
 * The only parameter that is optional is x11_display - it may be set
 * to NULL if there is no X11 server associated with the session.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 */
dbus_bool_t   ckc_create_local_session (CKConnector *ckc, uid_t user, const char *tty, const char *x11_display);

dbus_bool_t   ckc_create_local_session2 (CKConnector *ckc);

/* Gets the cookie that should be set as XDG_SESSION_COOKIE in the
 * users environment.
 *
 * Returns NULL unless ckc_create_local_session() succeeded 
 */
char         *ckc_get_cookie           (CKConnector *ckc);

/* Issues the CloseSession method call on the ConsoleKit manager
 * interface.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running, if the caller doesn't have
 * sufficient privilege or if ckc_create_local_session() wasn't
 * successfully invoked.
 */
dbus_bool_t   ckc_close_session        (CKConnector *ckc);

/* Frees all resources allocated and disconnects from the system
 * message bus.
 */
void          ckc_free                 (CKConnector *ckc);

/* example code:

#include <stdio.h>
#include <stdlib.h>
#include "ck-connector.h"

int
main (int argc, char *argv[])
{
	CKConnector *ckc;

	ckc = ckc_new ();
	if (ckc == NULL) {
		printf ("OOM creating CKConnector\n");
		goto out;
	}

	if (!ckc_create_local_session (ckc, 500, "/dev/tty2", ":1")) {
		printf ("cannot create CK session: OOM, D-Bus system bus not available,\n"
			"ConsoleKit not available or insufficient privileges.\n");
		goto out;
	}

	printf ("Session cookie is '%s'\n", ckc_get_cookie (ckc));
	sleep (20);

	if (!ckc_close_session (ckc)) {
		printf ("Cannot close CK session: OOM, D-Bus system bus not available,\n"
			"ConsoleKit not available or insufficient privileges.\n");
		goto out;
	}

out:
	if (ckc != NULL) {
		ckc_free (ckc);
	}
}

*/

#endif /* CK_CONNECTOR_H */



