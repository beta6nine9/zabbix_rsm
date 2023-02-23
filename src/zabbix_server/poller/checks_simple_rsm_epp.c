/*
** Zabbix
** Copyright (C) 2001-2013 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "sysinfo.h"
#include "base64.h"
#include "md5.h"
#include "rsm.h"
#include "log.h"
#include "checks_simple_rsm.h"

#include <openssl/ssl.h>

#define ZBX_EPP_LOG_PREFIX	"epp"	/* file will be <LOGDIR>/<PROBE>-<TLD>-ZBX_EPP_LOG_PREFIX.log */

#define XML_PATH_SERVER_ID	0
#define XML_PATH_RESULT_CODE	1

#define XML_VALUE_BUF_SIZE	512

#define EPP_SUCCESS_CODE_GENERAL	"1000"
#define EPP_SUCCESS_CODE_LOGOUT		"1500"

#define COMMAND_LOGIN	"login"
#define COMMAND_INFO	"info"
#define COMMAND_UPDATE	"update"
#define COMMAND_LOGOUT	"logout"

extern const char	epp_passphrase[128];

static int	epp_recv_buf(SSL *ssl, void *buf, int num)
{
	void	*p;
	int	read, ret = FAIL;

	if (1 > num)
		goto out;

	p = buf;

	while (0 < num)
	{
		if (0 >= (read = SSL_read(ssl, p, num)))
			goto out;

		p = (char *)p + read;
		num -= read;
	}

	ret = SUCCEED;
out:
	return ret;
}

static int	epp_recv_message(SSL *ssl, char **data, size_t *data_len, FILE *log_fd)
{
	unsigned int	message_size;
	int		ret = FAIL;

	if (NULL == data || NULL != *data)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	/* receive header */
	if (SUCCEED != epp_recv_buf(ssl, &message_size, sizeof(message_size)))
		goto out;

	*data_len = ntohl(message_size) - sizeof(message_size);
	*data = (char *)malloc(*data_len);

	/* receive body */
	if (SUCCEED != epp_recv_buf(ssl, *data, (int)*data_len - 1))
		goto out;

	(*data)[*data_len - 1] = '\0';

	rsm_infof(log_fd, "received message ===>\n%s\n<===", *data);

	ret = SUCCEED;
out:
	if (SUCCEED != ret && NULL != *data)
	{
		free(*data);
		*data = NULL;
	}

	return ret;
}

static int	epp_send_buf(SSL *ssl, const void *buf, int num)
{
	const void	*p;
	int		written, ret = FAIL;

	if (1 > num)
		goto out;

	p = buf;

	while (0 < num)
	{
		if (0 >= (written = SSL_write(ssl, p, num)))
			goto out;

		p = (const char *)p + written;
		num -= written;
	}

	ret = SUCCEED;
out:
	return ret;
}

static int	epp_send_message(SSL *ssl, const char *data, size_t data_size, FILE *log_fd)
{
	int		ret = FAIL;
	unsigned int	message_size;

	message_size = htonl((unsigned int)(data_size + sizeof(message_size)));

	/* send header */
	if (SUCCEED != epp_send_buf(ssl, &message_size, sizeof(message_size)))
		goto out;

	/* send body */
	if (SUCCEED != epp_send_buf(ssl, data, (int)data_size))
		goto out;

	rsm_infof(log_fd, "sent message ===>\n%s\n<===", data);

	ret = SUCCEED;
out:
	return ret;
}

static int	get_xml_value(const char *data, int xml_path, char *xml_value, size_t xml_value_size)
{
	const char	*p_start, *p_end, *start_tag, *end_tag;
	int		ret = FAIL;

	switch (xml_path)
	{
		case XML_PATH_SERVER_ID:
			start_tag = "<svID>";
			end_tag = "</svID>";
			break;
		case XML_PATH_RESULT_CODE:
			start_tag = "<result code=\"";
			end_tag = "\">";
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	if (NULL == (p_start = zbx_strcasestr(data, start_tag)))
		goto out;

	p_start += strlen(start_tag);

	if (NULL == (p_end = zbx_strcasestr(p_start, end_tag)))
		goto out;

	zbx_strlcpy(xml_value, p_start, MIN((size_t)(p_end - p_start + 1), xml_value_size));

	ret = SUCCEED;
out:
	return ret;
}

static int	get_tmpl(const char *epp_commands, const char *command, char **tmpl)
{
	char	buf[256];
	size_t	tmpl_alloc = 512, tmpl_offset = 0;
	int	f, nbytes, ret = FAIL;

	if (NULL == epp_commands)
		goto out;

	zbx_snprintf(buf, sizeof(buf), "%s/%s.tmpl", epp_commands, command);

	if (-1 == (f = zbx_open(buf, O_RDONLY)))
		goto out;

	*tmpl = (char *)zbx_malloc(*tmpl, tmpl_alloc);

	while (0 < (nbytes = zbx_read(f, buf, sizeof(buf), "")))
		zbx_strncpy_alloc(tmpl, &tmpl_alloc, &tmpl_offset, buf, (size_t)nbytes);

	if (-1 == nbytes)
	{
		zbx_free(*tmpl);
		goto out;
	}

	ret = SUCCEED;
out:
	if (-1 != f)
		close(f);

	return ret;
}

static int	get_first_message(SSL *ssl, int *res, FILE *log_fd, const char *epp_serverid, char *err, size_t err_size)
{
	char	xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t	data_len;
	int	ret = FAIL;

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_strlcpy(err, "cannot receive first message from server", err_size);
		*res = ZBX_EC_EPP_FIRSTTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_SERVER_ID, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no Server ID in first message from server");
		*res = ZBX_EC_EPP_FIRSTINVAL;
		goto out;
	}

	if (0 != strcmp(epp_serverid, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid Server ID in the first message from server: \"%s\""
				" (expected \"%s\")", xml_value, epp_serverid);
		*res = ZBX_EC_EPP_FIRSTINVAL;
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != data)
		free(data);

	return ret;
}

static void	rsm_tmpl_replace(char **tmpl, const char *variable, const char *value)
{
	const char	*p;
	size_t		variable_size, l_pos, r_pos;

	variable_size = strlen(variable);

	while (NULL != (p = strstr(*tmpl, variable)))
	{
		l_pos = (size_t)(p - *tmpl);
		r_pos = l_pos + variable_size - 1;

		zbx_replace_string(tmpl, (size_t)(p - *tmpl), &r_pos, value);
	}
}

static int	command_login(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_user, const char *epp_passwd, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t		data_len;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	rsm_tmpl_replace(&tmpl, "{TMPL_EPP_USER}", epp_user);
	rsm_tmpl_replace(&tmpl, "{TMPL_EPP_PASSWD}", epp_passwd);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_LOGINTO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_LOGINTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_LOGININVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_LOGININVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_update(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_testprefix, const char *domain, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL, tsbuf[32], buf[ZBX_HOST_BUF_SIZE];
	size_t		data_len;
	time_t		now;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	time(&now);
	zbx_snprintf(tsbuf, sizeof(tsbuf), "%llu", (unsigned long long)now);

	zbx_snprintf(buf, sizeof(buf), "%s.%s", epp_testprefix, domain);

	rsm_tmpl_replace(&tmpl, "{TMPL_DOMAIN}", buf);
	rsm_tmpl_replace(&tmpl, "{TMPL_TIMESTAMP}", tsbuf);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_UPDATETO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_UPDATETO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_UPDATEINVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_UPDATEINVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_info(const char *epp_commands, const char *name, SSL *ssl, int *rtt, FILE *log_fd,
		const char *epp_testprefix, const char *domain, char *err, size_t err_size)
{
	char		*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL, buf[ZBX_HOST_BUF_SIZE];
	size_t		data_len;
	zbx_timespec_t	start, end;
	int		ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	zbx_snprintf(buf, sizeof(buf), "%s.%s", epp_testprefix, domain);

	rsm_tmpl_replace(&tmpl, "{TMPL_DOMAIN}", buf);

	zbx_timespec(&start);

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		*rtt = ZBX_EC_EPP_INFOTO;
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		*rtt = ZBX_EC_EPP_INFOTO;
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		*rtt = ZBX_EC_EPP_INFOINVAL;
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_GENERAL, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_GENERAL);
		*rtt = ZBX_EC_EPP_INFOINVAL;
		goto out;
	}

	zbx_timespec(&end);
	*rtt = (end.sec - start.sec) * 1000 + (end.ns - start.ns) / 1000000;

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	command_logout(const char *epp_commands, const char *name, SSL *ssl, FILE *log_fd, char *err, size_t err_size)
{
	char	*tmpl = NULL, xml_value[XML_VALUE_BUF_SIZE], *data = NULL;
	size_t	data_len;
	int	ret = FAIL;

	if (SUCCEED != get_tmpl(epp_commands, name, &tmpl))
	{
		zbx_snprintf(err, err_size, "cannot load template \"%s\"", name);
		goto out;
	}

	if (SUCCEED != epp_send_message(ssl, tmpl, strlen(tmpl), log_fd))
	{
		zbx_snprintf(err, err_size, "cannot send command \"%s\"", name);
		goto out;
	}

	if (SUCCEED != epp_recv_message(ssl, &data, &data_len, log_fd))
	{
		zbx_snprintf(err, err_size, "cannot receive reply to command \"%s\"", name);
		goto out;
	}

	if (SUCCEED != get_xml_value(data, XML_PATH_RESULT_CODE, xml_value, sizeof(xml_value)))
	{
		zbx_snprintf(err, err_size, "no result code in reply");
		goto out;
	}

	if (0 != strcmp(EPP_SUCCESS_CODE_LOGOUT, xml_value))
	{
		zbx_snprintf(err, err_size, "invalid result code in reply to \"%s\": \"%s\" (expected \"%s\")",
				name, xml_value, EPP_SUCCESS_CODE_LOGOUT);
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(data);
	zbx_free(tmpl);

	return ret;
}

static int	rsm_ssl_attach_cert(SSL *ssl, char *cert, size_t cert_len, int *rtt, char *err, size_t err_size)
{
	BIO	*bio = NULL;
	X509	*x509 = NULL;
	int	ret = FAIL;

	if (NULL == (bio = BIO_new_mem_buf(cert, (int)cert_len)))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (NULL == (x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL)))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		/*rsm_ssl_get_error(err, err_size);*/
		goto out;
	}

	if (1 != SSL_use_certificate(ssl, x509))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		/*rsm_ssl_get_error(err, err_size);*/
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != x509)
		X509_free(x509);

	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static int	rsm_ssl_attach_privkey(SSL *ssl, char *privkey, size_t privkey_len, int *rtt, char *err, size_t err_size)
{
	BIO	*bio = NULL;
	RSA	*rsa = NULL;
	int	ret = FAIL;

	if (NULL == (bio = BIO_new_mem_buf(privkey, (int)privkey_len)))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (NULL == (rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL)))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		/*rsm_ssl_get_error(err, err_size);*/
		goto out;
	}

	if (1 != SSL_use_RSAPrivateKey(ssl, rsa))
	{
		*rtt = ZBX_EC_EPP_CRYPT;
		/*rsm_ssl_get_error(err, err_size);*/
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != rsa)
		RSA_free(rsa);

	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static char	*rsm_parse_time(char *str, size_t str_size, int *i)
{
	char	*p_end;
	char	c;
	size_t	block_size = 0;
	int	rv;

	p_end = str;

	while ('\0' != *p_end && block_size++ < str_size)
		p_end++;

	if (str == p_end)
		return NULL;

	c = *p_end;
	*p_end = '\0';

	rv = sscanf(str, "%d", i);
	*p_end = c;

	if (1 != rv)
		return NULL;


	return p_end;
}

static int	rsm_parse_asn1time(ASN1_TIME *asn1time, time_t *time, char *err, size_t err_size)
{
	struct tm	tm;
	char		buf[15], *p;
	int		ret = FAIL;

	if (V_ASN1_UTCTIME == asn1time->type && 13 == asn1time->length && 'Z' == asn1time->data[12])
	{
		memcpy(buf + 2, asn1time->data, (size_t)asn1time->length - 1);

		if ('5' <= asn1time->data[0])
		{
			buf[0] = '1';
			buf[1] = '9';
		}
		else
		{
			buf[0] = '2';
			buf[1] = '0';
		}
	}
	else if (V_ASN1_GENERALIZEDTIME == asn1time->type && 15 == asn1time->length && 'Z' == asn1time->data[14])
	{
		memcpy(buf, asn1time->data, (size_t)asn1time->length - 1);
	}
	else
	{
		zbx_strlcpy(err, "unknown date format", err_size);
		goto out;
	}

	buf[14] = '\0';

	memset(&tm, 0, sizeof(tm));

	/* year */
	if (NULL == (p = rsm_parse_time(buf, 4, &tm.tm_year)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid year", err_size);
		goto out;
	}

	/* month */
	if (NULL == (p = rsm_parse_time(p, 2, &tm.tm_mon)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid month", err_size);
		goto out;
	}

	/* day of month */
	if (NULL == (p = rsm_parse_time(p, 2, &tm.tm_mday)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid day of month", err_size);
		goto out;
	}

	/* hours */
	if (NULL == (p = rsm_parse_time(p, 2, &tm.tm_hour)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid hours", err_size);
		goto out;
	}

	/* minutes */
	if (NULL == (p = rsm_parse_time(p, 2, &tm.tm_min)) || '\0' == *p)
	{
		zbx_strlcpy(err, "invalid minutes", err_size);
		goto out;
	}

	/* seconds */
	if (NULL == (p = rsm_parse_time(p, 2, &tm.tm_sec)) || '\0' != *p)
	{
		zbx_strlcpy(err, "invalid seconds", err_size);
		goto out;
	}

	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	*time = timegm(&tm);

	ret = SUCCEED;
out:
	return ret;
}

static int	rsm_get_cert_md5(X509 *cert, char **md5, char *err, size_t err_size)
{
	char		*data;
	BIO		*bio;
	long		len;
	size_t		sz, i;
	md5_state_t	state;
	md5_byte_t	hash[MD5_DIGEST_SIZE];
	int		ret = FAIL;

	if (NULL == (bio = BIO_new(BIO_s_mem())))
	{
		zbx_strlcpy(err, "out of memory", err_size);
		goto out;
	}

	if (1 != PEM_write_bio_X509(bio, cert))
	{
		zbx_strlcpy(err, "internal OpenSSL error while parsing server certificate", err_size);
		goto out;
	}

	len = BIO_get_mem_data(bio, &data);	/* "data" points to the cert data (no need to free), len - its length */

	zbx_md5_init(&state);
	zbx_md5_append(&state, (const md5_byte_t *)data, (int)len);
	zbx_md5_finish(&state, hash);

	sz = MD5_DIGEST_SIZE * 2 + 1;
	*md5 = (char *)zbx_malloc(*md5, sz);

	for (i = 0; i < MD5_DIGEST_SIZE; i++)
		zbx_snprintf(&(*md5)[i << 1], sz - (i << 1), "%02x", hash[i]);

	ret = SUCCEED;
out:
	if (NULL != bio)
		BIO_free(bio);

	return ret;
}

static int	rsm_validate_cert(X509 *cert, const char *md5_macro, int *rtt, char *err, size_t err_size)
{
	time_t	now, not_before, not_after;
	char	*md5 = NULL;
	int	ret = FAIL;

	/* get certificate validity dates */
	if (SUCCEED != rsm_parse_asn1time(X509_get_notBefore(cert), &not_before, err, err_size))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		goto out;
	}

	if (SUCCEED != rsm_parse_asn1time(X509_get_notAfter(cert), &not_after, err, err_size))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		goto out;
	}

	now = time(NULL);
	if (now > not_after)
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_strlcpy(err, "the certificate has expired", err_size);
		goto out;
	}

	if (now < not_before)
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_strlcpy(err, "the validity date is in the future", err_size);
		goto out;
	}

	if (SUCCEED != rsm_get_cert_md5(cert, &md5, err, err_size))
	{
		*rtt = ZBX_EC_EPP_INTERNAL_GENERAL;
		goto out;
	}

	if (0 != strcmp(md5_macro, md5))
	{
		*rtt = ZBX_EC_EPP_SERVERCERT;
		zbx_snprintf(err, err_size, "MD5 sum set in a macro (%s) differs from what we got (%s)", md5_macro, md5);
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_free(md5);

	return ret;
}

static void	str_base64_decode_dyn(const char *in, size_t in_size, char **out, size_t *out_size)
{
	*out = (char *)zbx_malloc(*out, in_size);

	str_base64_decode(in, *out, (int)in_size, (int *)out_size);
}

static void	rsm_delete_unsupported_ips(zbx_vector_str_t *ips, int ipv4_enabled, int ipv6_enabled)
{
	int	i;
	char	is_ipv4;

	for (i = 0; i < ips->values_num; i++)
	{
		if (SUCCEED != rsm_validate_ip(ips->values[i], ipv4_enabled, ipv6_enabled, NULL, &is_ipv4))
		{
			zbx_free(ips->values[i]);
			zbx_vector_str_remove(ips, i--);

			continue;
		}

		if ((0 != is_ipv4 && 0 == ipv4_enabled) || (0 == is_ipv4 && 0 == ipv6_enabled))
		{
			zbx_free(ips->values[i]);
			zbx_vector_str_remove(ips, i--);
		}
	}
}

int	check_rsm_epp(const char *host, const AGENT_REQUEST *request, AGENT_RESULT *result)
{
	ldns_resolver		*res = NULL;
	rsm_resolver_error_t	ec_res;
	char			*rsmhost,
				err[ZBX_ERR_BUF_SIZE],
				*value_str = NULL,
				*res_ip = NULL,
				*secretkey_enc_b64 = NULL,
				*secretkey_salt_b64 = NULL,
				*epp_passwd_enc_b64 = NULL,
				*epp_passwd_salt_b64 = NULL,
				*epp_privkey_enc_b64 = NULL,
				*epp_privkey_salt_b64 = NULL,
				*epp_user = NULL,
				*epp_passwd = NULL,
				*epp_privkey = NULL,
				*epp_cert_b64 = NULL,
				*epp_cert = NULL,
				*epp_commands = NULL,
				*epp_serverid = NULL,
				*epp_testprefix = NULL,
				*epp_servercertmd5 = NULL;
	unsigned short		epp_port = 700;
	X509			*epp_server_x509 = NULL;
	const SSL_METHOD	*method;
	const char		*ip = NULL,
				*random_host;
	SSL_CTX			*ctx = NULL;
	SSL			*ssl = NULL;
	FILE			*log_fd = NULL;
	zbx_socket_t		sock;
	zbx_vector_str_t	epp_hosts,
				epp_ips;
	unsigned int		extras;
	uint16_t		resolver_port = DEFAULT_RESOLVER_PORT;
	size_t			epp_cert_size;
	int			rv,
				rtt,
				rtt1 = ZBX_NO_VALUE,
				rtt2 = ZBX_NO_VALUE,
				rtt3 = ZBX_NO_VALUE,
				ipv4_enabled = 0,
				ipv6_enabled = 0,
				ret = SYSINFO_RET_FAIL;

	zbx_vector_str_create(&epp_hosts);
	zbx_vector_str_create(&epp_ips);

	if (2 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "item must contain 2 parameters"));
		goto out;
	}

	rsmhost = get_rparam(request, 0);

	if ('\0' == *rsmhost)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "first parameter missing"));
		goto out;
	}

	/* open log file */
	if (SUCCEED != start_test(&log_fd, NULL, host, rsmhost, ZBX_EPP_LOG_PREFIX, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, err));
		goto out;
	}

	if ('\0' == *epp_passphrase)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "EPP passphrase was not provided when starting proxy"
				" (restart proxy with --rsm option)"));
		goto out;
	}

	/* get EPP servers list */
	value_str = zbx_strdup(value_str, get_rparam(request, 1));

	if ('\0' == *value_str)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "second key parameter missing"));
		goto out;
	}

	rsm_get_strings_from_list(&epp_hosts, value_str, ',');

	if (0 == epp_hosts.values_num)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "cannot get EPP hosts from key parameter"));
		goto out;
	}

	/* TODO: make sure the service is enabled on TLD and Probe */

	/* TODO: get certificate, service ID, RT MD5, password and salt, client */
	/* private key and salt, EPP passphrase and salt and other things */

	/* TODO: find out if RESOLVER_EXTRAS_DNSSEC is correct choice */
	extras = RESOLVER_EXTRAS_DNSSEC;

	/* create resolver */
	if (SUCCEED != rsm_create_resolver(&res, "resolver", res_ip, resolver_port, RSM_TCP, ipv4_enabled, ipv6_enabled,
			extras, RSM_TCP_TIMEOUT, RSM_TCP_RETRY, log_fd, err, sizeof(err)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "cannot create resolver: %s", err));
		goto out;
	}

	/* from this point item will not become NOTSUPPORTED */
	ret = SYSINFO_RET_OK;

	if (1)/*if (SUCCEED != rsm_ssl_init())*/
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot initialize SSL library");
		goto out;
	}

	/* set SSLv2 client hello, also announce SSLv3 and TLSv1 */
	method = SSLv23_client_method();

	/* create a new SSL context */
	if (NULL == (ctx = SSL_CTX_new(method)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot create a new SSL context structure");
		goto out;
	}

	/* disabling SSLv2 will leave v3 and TSLv1 for negotiation */
	SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);

	/* create new SSL connection state object */
	if (NULL == (ssl = SSL_new(ctx)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot create a new SSL context structure");
		goto out;
	}

	/* choose random host */
	random_host = epp_hosts.values[rsm_random((size_t)epp_hosts.values_num)];

	/* resolve host to ips: TODO! error handler functions not implemented (see NULLs below) */
	if (SUCCEED != rsm_resolve_host(res, random_host, &epp_ips,
			(0 != ipv4_enabled ? ZBX_FLAG_IPV4_ENABLED : 0) | (0 != ipv6_enabled ? ZBX_FLAG_IPV6_ENABLED : 0),
			log_fd, &ec_res, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = (ZBX_RESOLVER_NOREPLY != ec_res ? ZBX_EC_EPP_NO_IP : ZBX_EC_EPP_INTERNAL_GENERAL);
		rsm_errf(log_fd, "\"%s\": %s", random_host, err);
		goto out;
	}

	rsm_delete_unsupported_ips(&epp_ips, ipv4_enabled, ipv6_enabled);

	if (0 == epp_ips.values_num)
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_RDAP_INTERNAL_IP_UNSUP;
		rsm_errf(log_fd, "EPP \"%s\": IP address(es) of host not supported by this probe", random_host);
		goto out;
	}

	/* choose random IP */
	ip = epp_ips.values[rsm_random((size_t)epp_ips.values_num)];

	/* make the underlying TCP socket connection */
	if (SUCCEED != zbx_tcp_connect(&sock, NULL, ip, epp_port, RSM_TCP_TIMEOUT,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_CONNECT;
		rsm_errf(log_fd, "cannot connect to EPP server %s:%d", ip, epp_port);
		goto out;
	}

	/* attach the socket descriptor to SSL session */
	if (1 != SSL_set_fd(ssl, sock.socket))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "cannot attach TCP socket to SSL session");
		goto out;
	}

	if (epp_cert_b64 == NULL)
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_err(log_fd, "no EPP certificate");
		goto out;
	}

	str_base64_decode_dyn(epp_cert_b64, strlen(epp_cert_b64), &epp_cert, &epp_cert_size);

	if (SUCCEED != rsm_ssl_attach_cert(ssl, epp_cert, epp_cert_size, &rtt, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "cannot attach client certificate to SSL session: %s", err);
		goto out;
	}

	if (SUCCEED != decrypt_ciphertext(epp_passphrase, strlen(epp_passphrase), secretkey_enc_b64,
			strlen(secretkey_enc_b64), secretkey_salt_b64, strlen(secretkey_salt_b64), epp_privkey_enc_b64,
			strlen(epp_privkey_enc_b64), epp_privkey_salt_b64, strlen(epp_privkey_salt_b64), &epp_privkey,
			err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "cannot decrypt client private key: %s", err);
		goto out;
	}

	rv = rsm_ssl_attach_privkey(ssl, epp_privkey, strlen(epp_privkey), &rtt, err, sizeof(err));

	memset(epp_privkey, 0, strlen(epp_privkey));
	zbx_free(epp_privkey);

	if (SUCCEED != rv)
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "cannot attach client private key to SSL session: %s", err);
		goto out;
	}

	/* try to SSL-connect, returns 1 on success */
	if (1 != SSL_connect(ssl))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		/*rsm_ssl_get_error(err, sizeof(err));*/
		rsm_errf(log_fd, "cannot build an SSL connection to %s:%d: %s", ip, epp_port, err);
		goto out;
	}

	/* get the remote certificate into the X509 structure */
	if (NULL == (epp_server_x509 = SSL_get_peer_certificate(ssl)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_SERVERCERT;
		rsm_errf(log_fd, "cannot get Server certificate from %s:%d", ip, epp_port);
		goto out;
	}

	if (SUCCEED != rsm_validate_cert(epp_server_x509, epp_servercertmd5, &rtt, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rtt;
		rsm_errf(log_fd, "Server certificate validation failed: %s", err);
		goto out;
	}

	rsm_info(log_fd, "Server certificate validation successful");

	rsm_infof(log_fd, "start EPP test (ip %s)", ip);

	if (SUCCEED != get_first_message(ssl, &rv, log_fd, epp_serverid, err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = rv;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != decrypt_ciphertext(epp_passphrase, strlen(epp_passphrase), secretkey_enc_b64,
			strlen(secretkey_enc_b64), secretkey_salt_b64, strlen(secretkey_salt_b64), epp_passwd_enc_b64,
			strlen(epp_passwd_enc_b64), epp_passwd_salt_b64, strlen(epp_passwd_salt_b64), &epp_passwd,
			err, sizeof(err)))
	{
		rtt1 = rtt2 = rtt3 = ZBX_EC_EPP_INTERNAL_GENERAL;
		rsm_errf(log_fd, "cannot decrypt EPP password: %s", err);
		goto out;
	}

	rv = command_login(epp_commands, COMMAND_LOGIN, ssl, &rtt1, log_fd, epp_user, epp_passwd, err, sizeof(err));

	memset(epp_passwd, 0, strlen(epp_passwd));
	zbx_free(epp_passwd);

	if (SUCCEED != rv)
	{
		rtt2 = rtt3 = rtt1;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != command_update(epp_commands, COMMAND_UPDATE, ssl, &rtt2, log_fd, epp_testprefix, rsmhost,
			err, sizeof(err)))
	{
		rtt3 = rtt2;
		rsm_err(log_fd, err);
		goto out;
	}

	if (SUCCEED != command_info(epp_commands, COMMAND_INFO, ssl, &rtt3, log_fd, epp_testprefix, rsmhost, err,
			sizeof(err)))
	{
		rsm_err(log_fd, err);
		goto out;
	}

	/* logout command errors should not affect the test results */
	if (SUCCEED != command_logout(epp_commands, COMMAND_LOGOUT, ssl, log_fd, err, sizeof(err)))
		rsm_err(log_fd, err);

	rsm_infof(log_fd, "end EPP test (ip %s):SUCCESS", ip);
out:
	if (0 != ISSET_MSG(result))
	{
		rsm_err(log_fd, result->msg);
	}
	else
	{
		/* TODO: save result: ip, rtt1, rtt2, rtt3 */
	}

	zbx_free(epp_servercertmd5);
	zbx_free(epp_testprefix);
	zbx_free(epp_serverid);
	zbx_free(epp_commands);
	zbx_free(epp_user);
	zbx_free(epp_cert);
	zbx_free(epp_cert_b64);
	zbx_free(epp_privkey_salt_b64);
	zbx_free(epp_privkey_enc_b64);
	zbx_free(epp_passwd_salt_b64);
	zbx_free(epp_passwd_enc_b64);
	zbx_free(secretkey_salt_b64);
	zbx_free(secretkey_enc_b64);

	if (NULL != epp_server_x509)
		X509_free(epp_server_x509);

	if (NULL != ssl)
	{
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}

	if (NULL != ctx)
		SSL_CTX_free(ctx);

	zbx_tcp_close(&sock);

	zbx_free(value_str);
	zbx_free(res_ip);

	rsm_vector_str_clean_and_destroy(&epp_ips);
	rsm_vector_str_clean_and_destroy(&epp_hosts);

	end_test(log_fd, NULL);

	return ret;
}
