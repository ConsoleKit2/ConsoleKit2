#include <sys/types.h>

int sd_session_get_class(const char *session, char **class);
int sd_session_get_seat(const char *session, char **seat);
int sd_session_get_type(const char *session, char **type);
int sd_session_get_state(const char *session, char **state);
int sd_session_get_uid(const char *session, uid_t *uid);
int sd_uid_get_sessions(uid_t uid, int require_active, char ***sessions);

int sd_seat_can_multi_session(const char *seat);

int sd_get_sessions(char ***sessions);
