#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "rsm.h"
#include "missingconf.h"

unsigned char	program_type = ZBX_PROGRAM_TYPE_GET;

const char      *progname = NULL;
const char	title_message[] = "Zabbix decrypter";
const char	syslog_app_name[] = "rsm_epp_dec";
const char	*help_message[] = {NULL};
const char	*usage_message[] = {
	"<secretkey_enc_b64> <secretkey_salt_b64> <sensdata_enc_b64> <sensdata_salt_b64>", NULL,
	"Decrypt sensitive data using secret key.", NULL,
	"Parameters:", NULL,
	"    <secretkey_enc_b64>         secret key, encrypted with passphrase, base64-encoded", NULL,
	"    <secretkey_salt_b64>        secret key salt, base64-encoded", NULL,
	"    <sensdata_enc_b64>          encryped sensitive data, base64-encoded", NULL,
	"    <sensdata_salt_b64>         sensitive data salt, base64-encoded", NULL,
	NULL	/* end of text */
};

int	main(int argc, char *argv[])
{
	const char	*secretkey_enc_b64, *secretkey_salt_b64, *sensdata_enc_b64, *sensdata_salt_b64;
	char		passphrase[RSM_EPP_PASSPHRASE_MAX], *sensdata = NULL, err[128];

	progname = get_program_name(argv[0]);

	if (argc != 5)
	{
		usage();
		exit(1);
	}

	secretkey_enc_b64 = argv[1];
	secretkey_salt_b64 = argv[2];
	sensdata_enc_b64 = argv[3];
	sensdata_salt_b64 = argv[4];

	if (SUCCEED != zbx_read_stdin("Enter EPP passphrase: ", passphrase, sizeof(passphrase), err, sizeof(err)))
	{
		fprintf(stderr, "cannot get EPP passphrase: %s\n", err);
		goto out;
	}

	/* initialize the library */
	if (SUCCEED != rsm_ssl_init())
	{
		fprintf(stderr, "cannot initialize SSL library\n");
		goto out;
	}

	if (SUCCEED != decrypt_ciphertext(passphrase, strlen(passphrase), secretkey_enc_b64, strlen(secretkey_enc_b64),
			secretkey_salt_b64, strlen(secretkey_salt_b64), sensdata_enc_b64, strlen(sensdata_enc_b64),
			sensdata_salt_b64, strlen(sensdata_salt_b64), &sensdata, err, sizeof(err)))
	{
		fprintf(stderr, "cannot encrypt sensitive data: %s\n", err);
		goto out;
	}

	printf("sensitive data: %s\n", sensdata);
out:
	zbx_free(sensdata);

	return 0;
}
