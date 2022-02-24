#ifndef _T_RSM_H_
#define _T_RSM_H_

#include <signal.h>

int	write_json_status(const char *json_file, const char *buffer, char **error);
void	alarm_signal_handler(int sig, siginfo_t *siginfo, void *context);

#endif	/* _T_RSM_H_ */
