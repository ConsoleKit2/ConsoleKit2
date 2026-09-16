#pragma once

#include <sys/types.h>
#include <stdint.h>

typedef struct
{
    int kq;
    GDBusProxy *manager;
    const char *category;
} sd_login_monitor;

int sd_login_monitor_new(const char *category, sd_login_monitor **ret);
sd_login_monitor *sd_login_monitor_unref(sd_login_monitor *monitor);
void sd_login_monitor_unrefp(sd_login_monitor **monitor);
int sd_login_monitor_flush(sd_login_monitor *monitor);
int sd_login_monitor_get_fd(sd_login_monitor *monitor);
int sd_login_monitor_get_events(sd_login_monitor *monitor);
int sd_login_monitor_get_timeout(sd_login_monitor *monitor, uint64_t *timeout_usec);

int sd_session_get_class(const char *session, char **class);
int sd_session_get_seat(const char *session, char **seat);
int sd_session_get_type(const char *session, char **type);
int sd_session_get_state(const char *session, char **state);
int sd_session_get_uid(const char *session, uid_t *uid);
int sd_session_get_service(const char *session, char **service);
int sd_session_get_display(const char *session, char **display);

int sd_pid_get_session(pid_t pid, char **session);

int sd_uid_get_sessions(uid_t uid, int require_active, char ***sessions);

int sd_seat_can_graphical(const char *seat);
int sd_seat_can_multi_session(const char *seat);
int sd_seat_get_sessions(const char *seat, char ***sessions, uid_t **uid, unsigned int *n_uids);

int sd_get_sessions(char ***sessions);
