#include <inttypes.h>
#include <limits.h>
#include <safe_str_lib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "io.h"
#include "mbuffer.h"
#include "safe_lib.h"
#include "toml.h"

#define PREFIX long_int
#define T long int
#include "mbuffertemplate.h"

#define PREFIX size_t
#define T size_t
#include "mbuffertemplate.h"

#define PREFIX char
#define T char
#include "mbuffertemplate.h"

#define MIN(i, k) (((i) < (k)) ? (i) : (k))

int readconfig(const char path[], struct configInfo *config)
{
	int fd_config;

	config->maxcount = -1;
	config->logpath[0] = '\0';
	config->xkeymaps = false;
	config->minavrg.tv_sec = 0;
	config->minavrg.tv_nsec = 0;

	// Open the config file as read-only.
	fd_config = open(path, O_RDWR);
	if (fd_config < 0) {
		ERR("open");
		return -1;
	}

	// Try to lock the file so that only one instance of the daemon
	// can be run at a given time.
	{
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;

		lock.l_start = 0;
		lock.l_len = 0;

		if (fcntl(fd_config, F_SETLK, &lock)) {
			if (errno == EACCES || errno == EAGAIN) {
				LOG(-1,
				    "Another instance is probably running!\n");
			}
			ERR("fcntl");
			return -1;
		}
	}

	{
		FILE *p_config;
		char err_ret_buff[200];

		// Convert the file descriptor to a FILE pointer.
		p_config = fdopen(fd_config, "r");
		if (!p_config) {
			ERR("fdopen");
			return -1;
		}

		// Parse the configuration file and extract all values from the "config" table.
		toml_table_t *content = toml_parse_file(p_config, err_ret_buff,
							sizeof(err_ret_buff));
		if (!content) {
			ERR("toml_parse_file");
			return -1;
		}

		const toml_table_t *config_table =
			toml_table_in(content, "config");
		if (!config_table) {
			ERR("toml_table_in");
			return -1;
		}

		// Handle all possible configuration entries.
		const toml_datum_t minimum_avg =
			toml_int_in(config_table, "minimum_avg");
		if (minimum_avg.ok) {
			config->minavrg.tv_nsec = minimum_avg.u.i;
		}

		const toml_datum_t max_score =
			toml_int_in(config_table, "max_score");
		if (max_score.ok) {
			config->maxcount = max_score.u.i;
		}

		const toml_datum_t use_xkeymaps =
			toml_bool_in(config_table, "use_xkeymaps");
		if (use_xkeymaps.ok) {
			config->xkeymaps = use_xkeymaps.u.b;
		}

		const toml_datum_t daemon_log_path =
			toml_string_in(config_table, "daemon_log_path");
		if (daemon_log_path.ok) {
			strcpy_s(config->logpath, PATH_MAX,
				 daemon_log_path.u.s);
		}

		toml_free(content);
	}

	if (close(fd_config)) {
		ERR("close");
	}
	return 0;
}

int handleargs(int argc, char *argv[], struct argInfo *data)
{
	int i;
	bool unrecognized = false;
	bool help = false;
	data->configpath[0] = '\0';

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			// path to the config to be used
			case 'c':
				if (i + 1 < argc) {
					strcpy_s(data->configpath, PATH_MAX,
						 argv[i + 1]); // config path
				}
				break;

			// daemonize the daemon
			case 'd':
				g_daemonize = true;
				break;

			// increase the verbosity level
			case 'v':
				if (g_loglevel < MAX_LOGLEVEL) {
					g_loglevel++;
				} else {
					LOG(0,
					    "Can't increment loglevel any more!\n");
				}
				break;

			// shows help
			case 'h':
				printf("duckydd %s\n"
				       "Usage: duckydd [Options]\n"
				       "\t\t-c <file>\tSpecify a config file path\n"
				       "\t\t-d\t\tDaemonize the process\n"
				       "\t\t-v\t\tIncrease verbosity of the console output (The maximum verbosity is 2)\n"
				       "\t\t\t\tTHE -v OPTION CAN POTENTIALY EXPOSE PASSWORDS!!!\n"
				       "\t\t-h\t\tShows this help section\n\n"
				       "For config options please have a look at the README.md\n"
				       "The daemon was linked against: udev "
#ifdef ENABLE_XKB_EXTENSION
				       "xkbcommon xkbcommon-x11 xcb "
#endif
				       "\n",
				       GIT_VERSION);
				help = true;
				break;

			default:
				LOG(0, "%s is not a recognized option. \n",
				    argv[i]);
				unrecognized = true;
				break;
			}
		}
	}

	if (unrecognized) {
		LOG(0,
		    "One or more options where not recognized! You can try the -h argument for a list of supported options.\n");
	}

	if (help) {
		return -1;
	} else if (data->configpath[0] == '\0') {
		LOG(0, "Please provide a config location!\n");
		return -1;
	}
	return 0;
}

void _logger(short loglevel, const char func[], const char format[], ...)
{
	// check for a format string bigger than the max
	if (loglevel <= g_loglevel) {
		if (strnlen(func, MAX_SIZE_FORMAT_STRING) <=
			    MAX_SIZE_FORMAT_STRING &&
		    strnlen(format, MAX_SIZE_FUNCTION_NAME) <=
			    MAX_SIZE_FUNCTION_NAME) {
			va_list args;
			va_start(args, format);

			char appended[MAX_SIZE_FORMAT_STRING +
				      MAX_SIZE_FUNCTION_NAME];
			char prefix;
			FILE *fd;

			// change prefix depending on loglevel
			switch (loglevel) {
			case -1:
				prefix = '!';
				fd = stderr;
				break;

			default:
				prefix = '*';
				fd = stdout;
				break;
			}

			if (g_loglevel > 0) {
				sprintf(appended, "[%c][%s] %s", prefix, func,
					format);
			} else {
				sprintf(appended, "[%c] %s", prefix, format);
			}
			vfprintf(fd, appended, args);

			va_end(args);
		}
	}
}

errno_t pathcat(char path1[], const char path2[])
{
	if (strnlen_s(path1, PATH_MAX) + strnlen_s(path2, PATH_MAX) <
	    PATH_MAX) {
		return strcat_s(path1, PATH_MAX, path2);
	}
	return EINVAL;
}

errno_t pathcpy(char path1[], const char path2[])
{
	if (strnlen_s(path1, PATH_MAX) + strnlen_s(path2, PATH_MAX) <
	    PATH_MAX) {
		return strcpy_s(path1, PATH_MAX, path2);
	}
	return EINVAL;
}

// memsave strcmp functions
int strcmp_ss(const char str1[], const char str2[])
{
	size_t i = 0;

	while (str1[i] == str2[i]) {
		if (str1[i] == '\0' || str2[i] == '\0') {
			break;
		}
		i++;
	}

	return str1[i] - str2[i];
}

int strncmp_ss(const char str1[], const char str2[], size_t length)
{
	size_t i = 0;

	while (str1[i] == str2[i] && i < length) {
		if (str1[i] == '\0' || str2[i] == '\0') {
			break;
		}
		i++;
	}

	return str1[i] - str2[i];
}

// returns the filename from a path
const char *find_file(const char *input)
{
	size_t i;

	for (i = strnlen_s(input, PATH_MAX); i > 0;
	     i--) { // returns the filename
		if (input[i] == '/') {
			return &input[i + 1];
		}
	}
	return NULL;
}
