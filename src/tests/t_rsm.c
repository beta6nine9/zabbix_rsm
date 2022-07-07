#include "common.h"

#include <stdio.h>
#include <errno.h>

#include "t_rsm.h"

int	write_json_status(const char *json_file, const char *buffer, char **error)
{
	FILE	*f;
	int	ret = FAIL;

	if (NULL == (f = fopen(json_file, "w")))	/* w for write */
	{
		*error = zbx_dsprintf(*error, "cannot open file \"%s\" for writing: %s", json_file, strerror(errno));
		goto out;
	}

	if (1 > fwrite(buffer, 1, strlen(buffer), f))
	{
		*error = zbx_dsprintf(*error, "cannot write resulting JSON to file \"%s\"", json_file);
		goto out;
	}

	if (1 > fwrite("\n", sizeof(char), 1, f))
	{
		*error = zbx_dsprintf(*error, "cannot write resulting JSON to file \"%s\"", json_file);
		goto out;
	}

	ret = SUCCEED;
out:
	if (f != NULL)
		fclose(f);

	return ret;
}

void	alarm_signal_handler(int sig)
{
	ZBX_UNUSED(sig);

	zbx_alarm_flag_set(); /* set alarm flag */
}
