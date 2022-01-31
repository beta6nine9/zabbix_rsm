#ifndef _T_RSM_H_
#define _T_RSM_H_

#include <common.h>
#include <zbxtypes.h>

const char	epp_passphrase[];
const char      *progname = "";
const char      title_message[] = "";
const char      *usage_message[] = {NULL};
const char      *help_message[] = {0};
unsigned char	program_type = ZBX_PROGRAM_TYPE_TEST;
const char	syslog_app_name[] = "rsm_test";

//unsigned char	daemon_type;

int		process_num;
unsigned char	process_type;

int	CONFIG_ALERTER_FORKS;
int	CONFIG_DISCOVERER_FORKS;
int	CONFIG_HOUSEKEEPER_FORKS;
int	CONFIG_NODEWATCHER_FORKS;
int	CONFIG_PINGER_FORKS;
int	CONFIG_POLLER_FORKS;
int	CONFIG_UNREACHABLE_POLLER_FORKS;
int	CONFIG_HTTPPOLLER_FORKS;
int	CONFIG_IPMIPOLLER_FORKS;
int	CONFIG_TIMER_FORKS;
int	CONFIG_TRAPPER_FORKS;
int	CONFIG_SNMPTRAPPER_FORKS;
int	CONFIG_JAVAPOLLER_FORKS;
int	CONFIG_ESCALATOR_FORKS;
int	CONFIG_SELFMON_FORKS;
int	CONFIG_WATCHDOG_FORKS;
int	CONFIG_DATASENDER_FORKS;
int	CONFIG_HEARTBEAT_FORKS;
int	CONFIG_LISTEN_PORT;
char	*CONFIG_LISTEN_IP;
char	*CONFIG_SOURCE_IP;
int	CONFIG_TRAPPER_TIMEOUT;
int	CONFIG_HOUSEKEEPING_FREQUENCY;
int	CONFIG_MAX_HOUSEKEEPER_DELETE;
int	CONFIG_SENDER_FREQUENCY;
int	CONFIG_HISTSYNCER_FORKS;
int	CONFIG_HISTSYNCER_FREQUENCY;
int	CONFIG_CONFSYNCER_FORKS;
int	CONFIG_CONFSYNCER_FREQUENCY;
zbx_uint64_t	CONFIG_CONF_CACHE_SIZE;
zbx_uint64_t	CONFIG_HISTORY_CACHE_SIZE;
zbx_uint64_t	CONFIG_HISTORY_INDEX_CACHE_SIZE;
zbx_uint64_t	CONFIG_TRENDS_CACHE_SIZE;
zbx_uint64_t	CONFIG_TEXT_CACHE_SIZE;
zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE;
int	CONFIG_DISABLE_HOUSEKEEPING;
int	CONFIG_UNREACHABLE_PERIOD;
int	CONFIG_UNREACHABLE_DELAY;
int	CONFIG_UNAVAILABLE_DELAY;
int	CONFIG_LOG_LEVEL;
int	CONFIG_LOG_TYPE;
char	*CONFIG_LOG_TYPE_STR;
char	*CONFIG_ALERT_SCRIPTS_PATH;
char	*CONFIG_EXTERNALSCRIPTS;
char	*CONFIG_TMPDIR;
char	*CONFIG_FPING_LOCATION;
char	*CONFIG_FPING6_LOCATION;
char	*CONFIG_DBHOST;
char	*CONFIG_DBNAME;
char	*CONFIG_DBSCHEMA;
char	*CONFIG_DBUSER;
char	*CONFIG_DBPASSWORD;
char	*CONFIG_DBSOCKET;
int	CONFIG_DBPORT;
int	CONFIG_ENABLE_REMOTE_COMMANDS;
int	CONFIG_LOG_REMOTE_COMMANDS;
int	CONFIG_UNSAFE_USER_PARAMETERS;
int	CONFIG_NODEID;
int	CONFIG_MASTER_NODEID;
int	CONFIG_NODE_NOEVENTS;
int	CONFIG_NODE_NOHISTORY;
char	*CONFIG_SNMPTRAP_FILE;
char	*CONFIG_JAVA_GATEWAY;
int	CONFIG_JAVA_GATEWAY_PORT;
char	*CONFIG_SSH_KEY_LOCATION;
int	CONFIG_LOG_SLOW_QUERIES;
int	CONFIG_SERVER_STARTUP_TIME;
int	CONFIG_PROXYPOLLER_FORKS;
int	CONFIG_PROXYCONFIG_FREQUENCY;
int	CONFIG_PROXYDATA_FREQUENCY;
int	CONFIG_TIMEOUT;
const char	*CONFIG_LOG_FILE;
char	*CONFIG_FILE;
int	CONFIG_LOG_FILE_SIZE;

char	*CONFIG_DB_TLS_CONNECT		= NULL;
char	*CONFIG_DB_TLS_CERT_FILE	= NULL;
char	*CONFIG_DB_TLS_KEY_FILE		= NULL;
char	*CONFIG_DB_TLS_CA_FILE		= NULL;
char	*CONFIG_DB_TLS_CIPHER		= NULL;
char	*CONFIG_DB_TLS_CIPHER_13	= NULL;

char	*CONFIG_TLS_CIPHER_CERT13	= NULL;
char	*CONFIG_TLS_CIPHER_CERT		= NULL;
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;
char	*CONFIG_TLS_CIPHER_PSK		= NULL;
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;
char	*CONFIG_TLS_CIPHER_ALL		= NULL;
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;
char	*CONFIG_TLS_CIPHER_CMD		= NULL;

char	*CONFIG_TLS_PSK_FILE;
char	*CONFIG_TLS_CA_FILE;
char	*CONFIG_TLS_CRL_FILE;
char	*CONFIG_TLS_CERT_FILE;
char	*CONFIG_TLS_KEY_FILE;
char	*CONFIG_TLS_CONNECT;
char	*CONFIG_TLS_ACCEPT;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT;
char	*CONFIG_TLS_PSK_IDENTITY;
char	*CONFIG_TLS_SERVER_CERT_ISSUER;
int	CONFIG_PASSIVE_FORKS;
int	CONFIG_ACTIVE_FORKS;

int	CONFIG_DOUBLE_PRECISION		= ZBX_DB_DBL_PRECISION_ENABLED;

char	*CONFIG_HISTORY_STORAGE_URL;
char	*CONFIG_HISTORY_STORAGE_OPTS;
char	*CONFIG_HISTORY_STORAGE_PIPELINES;
char	*CONFIG_EXPORT_DIR;

zbx_uint64_t	*CONFIG_EXPORT_FILE_SIZE;

unsigned int	configured_tls_connect_mode;
unsigned int	configured_tls_accept_mode;
unsigned int	configured_tls_connect_modes;
unsigned int	configured_tls_accept_modes;

char	*CONFIG_EXPORT_TYPE	= NULL;

int	CONFIG_TCP_MAX_BACKLOG_SIZE	= SOMAXCONN;

ZBX_METRIC      parameters_common_http[] =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
{
	{"web.page.get",	CF_HAVEPARAMS,	NULL,			"localhost,,80"}
};

void	xml_escape_xpath(char **data) {}

#endif	/* _T_RSM_H_ */
