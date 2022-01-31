#include "common.h"
#include "t_rsm.h"

#include <stdio.h>
#include <errno.h>

int	write_json_status(const char *json_file, const char *buffer, char **error)
{
	FILE	*f;

	if (NULL == (f = fopen(json_file, "w")))	/* w for write */
	{
		*error = zbx_dsprintf(*error, "cannot open file \"%s\" for writing: %s", json_file, strerror(errno));
		return FAIL;
	}

	if (1 > fwrite(buffer, 1, strlen(buffer), f))
	{
		*error = zbx_dsprintf(*error, "cannot write resulting JSON to file \"%s\"", json_file);
		return FAIL;
	}

	if (1 > fwrite("\n", sizeof(char), 1, f))
	{
		*error = zbx_dsprintf(*error, "cannot write resulting JSON to file \"%s\"", json_file);
		return FAIL;
	}

	fclose(f);

	return SUCCEED;
}
