/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2011 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Edin Kadribasic <edink@php.net>                              |
   |         Marcus Boerger <helly@php.net>                               |
   |         Johannes Schlueter <johannes@php.net>                        |
   |         Parts based on CGI SAPI Module by                            |
   |         Rasmus Lerdorf, Stig Bakken and Zeev Suraski                 |
   +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php.h"
#include "php_globals.h"
#include "php_variables.h"
#include "zend_hash.h"
#include "zend_modules.h"
#include "zend_interfaces.h"

#include "ext/reflection/php_reflection.h"

#include "SAPI.h"

#include <stdio.h>
#include "php.h"
#ifdef PHP_WIN32
#include "win32/time.h"
#include "win32/signal.h"
#include <process.h>
#endif
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SIGNAL_H
#include <signal.h>
#endif
#if HAVE_SETLOCALE
#include <locale.h>
#endif
#include "zend.h"
#include "zend_extensions.h"
#include "php_ini.h"
#include "php_globals.h"
#include "php_main.h"
#include "fopen_wrappers.h"
#include "ext/standard/php_standard.h"
#include "cli.h"
#ifdef PHP_WIN32
#include <io.h>
#include <fcntl.h>
#include "win32/php_registry.h"
#endif

#if HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef __riscos__
#include <unixlib/local.h>
#endif

#include "zend_compile.h"
#include "zend_execute.h"
#include "zend_highlight.h"
#include "zend_indent.h"
#include "zend_exceptions.h"

#include "php_getopt.h"

#ifndef PHP_CLI_WIN32_NO_CONSOLE
#include "php_cli_server.h"
#endif

#ifndef PHP_WIN32
# define php_select(m, r, w, e, t)	select(m, r, w, e, t)
#else
# include "win32/select.h"
#endif

PHPAPI extern char *php_ini_opened_path;
PHPAPI extern char *php_ini_scanned_path;
PHPAPI extern char *php_ini_scanned_files;

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define PHP_MODE_STANDARD      1
#define PHP_MODE_HIGHLIGHT     2
#define PHP_MODE_INDENT        3
#define PHP_MODE_LINT          4
#define PHP_MODE_STRIP         5
#define PHP_MODE_CLI_DIRECT    6
#define PHP_MODE_PROCESS_STDIN 7
#define PHP_MODE_REFLECTION_FUNCTION    8
#define PHP_MODE_REFLECTION_CLASS       9
#define PHP_MODE_REFLECTION_EXTENSION   10
#define PHP_MODE_REFLECTION_EXT_INFO    11
#define PHP_MODE_REFLECTION_ZEND_EXTENSION 12
#define PHP_MODE_SHOW_INI_CONFIG        13

cli_shell_callbacks_t cli_shell_callbacks = { NULL, NULL, NULL };
PHP_CLI_API cli_shell_callbacks_t *php_cli_get_shell_callbacks()
{
	return &cli_shell_callbacks;
}

const char HARDCODED_INI[] =
	"html_errors=0\n"
	"register_argc_argv=1\n"
	"implicit_flush=1\n"
	"output_buffering=0\n"
	"max_execution_time=0\n"
	"max_input_time=-1\n\0";


const opt_struct OPTIONS[] = {
	{'a', 0, "interactive"},
	{'B', 1, "process-begin"},
	{'C', 0, "no-chdir"}, /* for compatibility with CGI (do not chdir to script directory) */
	{'c', 1, "php-ini"},
	{'d', 1, "define"},
	{'E', 1, "process-end"},
	{'e', 0, "profile-info"},
	{'F', 1, "process-file"},
	{'f', 1, "file"},
	{'h', 0, "help"},
	{'i', 0, "info"},
	{'l', 0, "syntax-check"},
	{'m', 0, "modules"},
	{'n', 0, "no-php-ini"},
	{'q', 0, "no-header"}, /* for compatibility with CGI (do not generate HTTP headers) */
	{'R', 1, "process-code"},
	{'H', 0, "hide-args"},
	{'r', 1, "run"},
	{'s', 0, "syntax-highlight"},
	{'s', 0, "syntax-highlighting"},
	{'S', 1, "server"},
	{'t', 1, "docroot"},
	{'w', 0, "strip"},
	{'?', 0, "usage"},/* help alias (both '?' and 'usage') */
	{'v', 0, "version"},
	{'z', 1, "zend-extension"},
	{10,  1, "rf"},
	{10,  1, "rfunction"},
	{11,  1, "rc"},
	{11,  1, "rclass"},
	{12,  1, "re"},
	{12,  1, "rextension"},
	{13,  1, "rz"},
	{13,  1, "rzendextension"},
	{14,  1, "ri"},
	{14,  1, "rextinfo"},
	{15,  0, "ini"},
	{'-', 0, NULL} /* end of args */
};

static int print_module_info(zend_module_entry *module TSRMLS_DC) /* {{{ */
{
	php_printf("%s\n", module->name);
	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

static int module_name_cmp(const void *a, const void *b TSRMLS_DC) /* {{{ */
{
	Bucket *f = *((Bucket **) a);
	Bucket *s = *((Bucket **) b);

	return strcasecmp(((zend_module_entry *)f->pData)->name,
				  ((zend_module_entry *)s->pData)->name);
}
/* }}} */

static void print_modules(TSRMLS_D) /* {{{ */
{
	HashTable sorted_registry;
	zend_module_entry tmp;

	zend_hash_init(&sorted_registry, 50, NULL, NULL, 1);
	zend_hash_copy(&sorted_registry, &module_registry, NULL, &tmp, sizeof(zend_module_entry));
	zend_hash_sort(&sorted_registry, zend_qsort, module_name_cmp, 0 TSRMLS_CC);
	zend_hash_apply(&sorted_registry, (apply_func_t) print_module_info TSRMLS_CC);
	zend_hash_destroy(&sorted_registry);
}
/* }}} */

static int print_extension_info(zend_extension *ext, void *arg TSRMLS_DC) /* {{{ */
{
	php_printf("%s\n", ext->name);
	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

static int extension_name_cmp(const zend_llist_element **f, const zend_llist_element **s TSRMLS_DC) /* {{{ */
{
	return strcmp(((zend_extension *)(*f)->data)->name,
				  ((zend_extension *)(*s)->data)->name);
}
/* }}} */

static void print_extensions(TSRMLS_D) /* {{{ */
{
	zend_llist sorted_exts;

	zend_llist_copy(&sorted_exts, &zend_extensions);
	sorted_exts.dtor = NULL;
	zend_llist_sort(&sorted_exts, extension_name_cmp TSRMLS_CC);
	zend_llist_apply(&sorted_exts, (llist_apply_func_t) print_extension_info TSRMLS_CC);
	zend_llist_destroy(&sorted_exts);
}
/* }}} */

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

static inline int sapi_cli_select(int fd TSRMLS_DC)
{
	fd_set wfd, dfd;
	struct timeval tv;
	int ret;

	FD_ZERO(&wfd);
	FD_ZERO(&dfd);

	PHP_SAFE_FD_SET(fd, &wfd);

	tv.tv_sec = FG(default_socket_timeout);
	tv.tv_usec = 0;

	ret = php_select(fd+1, &dfd, &wfd, &dfd, &tv);

	return ret != -1;
}

PHP_CLI_API size_t sapi_cli_single_write(const char *str, uint str_length TSRMLS_DC) /* {{{ */
{
#ifdef PHP_WRITE_STDOUT
	long ret;
#else
	size_t ret;
#endif

	if (cli_shell_callbacks.cli_shell_write) {
		size_t shell_wrote;
		shell_wrote = cli_shell_callbacks.cli_shell_write(str, str_length TSRMLS_CC);
		if (shell_wrote > -1) {
			return shell_wrote;
		}
	}

#ifdef PHP_WRITE_STDOUT
	do {
		ret = write(STDOUT_FILENO, str, str_length);
	} while (ret <= 0 && errno == EAGAIN && sapi_cli_select(STDOUT_FILENO TSRMLS_CC));

	if (ret <= 0) {
		return 0;
	}

	return ret;
#else
	ret = fwrite(str, 1, MIN(str_length, 16384), stdout);
	return ret;
#endif
}
/* }}} */

static int sapi_cli_ub_write(const char *str, uint str_length TSRMLS_DC) /* {{{ */
{
	const char *ptr = str;
	uint remaining = str_length;
	size_t ret;

	if (!str_length) {
		return 0;
	}

	if (cli_shell_callbacks.cli_shell_ub_write) {
		int ub_wrote;
		ub_wrote = cli_shell_callbacks.cli_shell_ub_write(str, str_length TSRMLS_CC);
		if (ub_wrote > -1) {
			return ub_wrote;
		}
	}

	while (remaining > 0)
	{
		ret = sapi_cli_single_write(ptr, remaining TSRMLS_CC);
		if (!ret) {
#ifndef PHP_CLI_WIN32_NO_CONSOLE
			php_handle_aborted_connection();
#endif
			break;
		}
		ptr += ret;
		remaining -= ret;
	}

	return (ptr - str);
}
/* }}} */

static void sapi_cli_flush(void *server_context) /* {{{ */
{
	/* Ignore EBADF here, it's caused by the fact that STDIN/STDOUT/STDERR streams
	 * are/could be closed before fflush() is called.
	 */
	if (fflush(stdout)==EOF && errno!=EBADF) {
#ifndef PHP_CLI_WIN32_NO_CONSOLE
		php_handle_aborted_connection();
#endif
	}
}
/* }}} */

static char *php_self = "";
static char *script_filename = "";

static void sapi_cli_register_variables(zval *track_vars_array TSRMLS_DC) /* {{{ */
{
	unsigned int len;
	char   *docroot = "";

	/* In CGI mode, we consider the environment to be a part of the server
	 * variables
	 */
	php_import_environment_variables(track_vars_array TSRMLS_CC);

	/* Build the special-case PHP_SELF variable for the CLI version */
	len = strlen(php_self);
	if (sapi_module.input_filter(PARSE_SERVER, "PHP_SELF", &php_self, len, &len TSRMLS_CC)) {
		php_register_variable("PHP_SELF", php_self, track_vars_array TSRMLS_CC);
	}
	if (sapi_module.input_filter(PARSE_SERVER, "SCRIPT_NAME", &php_self, len, &len TSRMLS_CC)) {
		php_register_variable("SCRIPT_NAME", php_self, track_vars_array TSRMLS_CC);
	}
	/* filenames are empty for stdin */
	len = strlen(script_filename);
	if (sapi_module.input_filter(PARSE_SERVER, "SCRIPT_FILENAME", &script_filename, len, &len TSRMLS_CC)) {
		php_register_variable("SCRIPT_FILENAME", script_filename, track_vars_array TSRMLS_CC);
	}
	if (sapi_module.input_filter(PARSE_SERVER, "PATH_TRANSLATED", &script_filename, len, &len TSRMLS_CC)) {
		php_register_variable("PATH_TRANSLATED", script_filename, track_vars_array TSRMLS_CC);
	}
	/* just make it available */
	len = 0U;
	if (sapi_module.input_filter(PARSE_SERVER, "DOCUMENT_ROOT", &docroot, len, &len TSRMLS_CC)) {
		php_register_variable("DOCUMENT_ROOT", docroot, track_vars_array TSRMLS_CC);
	}
}
/* }}} */

static void sapi_cli_log_message(char *message TSRMLS_DC) /* {{{ */
{
	fprintf(stderr, "%s\n", message);
}
/* }}} */

static int sapi_cli_deactivate(TSRMLS_D) /* {{{ */
{
	fflush(stdout);
	if(SG(request_info).argv0) {
		free(SG(request_info).argv0);
		SG(request_info).argv0 = NULL;
	}
	return SUCCESS;
}
/* }}} */

static char* sapi_cli_read_cookies(TSRMLS_D) /* {{{ */
{
	return NULL;
}
/* }}} */

static int sapi_cli_header_handler(sapi_header_struct *h, sapi_header_op_enum op, sapi_headers_struct *s TSRMLS_DC) /* {{{ */
{
	return 0;
}
/* }}} */

static int sapi_cli_send_headers(sapi_headers_struct *sapi_headers TSRMLS_DC) /* {{{ */
{
	/* We do nothing here, this function is needed to prevent that the fallback
	 * header handling is called. */
	return SAPI_HEADER_SENT_SUCCESSFULLY;
}
/* }}} */

static void sapi_cli_send_header(sapi_header_struct *sapi_header, void *server_context TSRMLS_DC) /* {{{ */
{
}
/* }}} */

static int php_cli_startup(sapi_module_struct *sapi_module) /* {{{ */
{
	if (php_module_startup(sapi_module, NULL, 0)==FAILURE) {
		return FAILURE;
	}
	return SUCCESS;
}
/* }}} */

/* {{{ sapi_cli_ini_defaults */

/* overwriteable ini defaults must be set in sapi_cli_ini_defaults() */
#define INI_DEFAULT(name,value)\
	Z_SET_REFCOUNT(tmp, 0);\
	Z_UNSET_ISREF(tmp);	\
	ZVAL_STRINGL(&tmp, zend_strndup(value, sizeof(value)-1), sizeof(value)-1, 0);\
	zend_hash_update(configuration_hash, name, sizeof(name), &tmp, sizeof(zval), NULL);\

static void sapi_cli_ini_defaults(HashTable *configuration_hash)
{
	zval tmp;
	INI_DEFAULT("report_zend_debug", "0");
	INI_DEFAULT("display_errors", "1");
}
/* }}} */

/* {{{ sapi_module_struct cli_sapi_module
 */
static sapi_module_struct cli_sapi_module = {
	"cli",							/* name */
	"Command Line Interface",    	/* pretty name */

	php_cli_startup,				/* startup */
	php_module_shutdown_wrapper,	/* shutdown */

	NULL,							/* activate */
	sapi_cli_deactivate,			/* deactivate */

	sapi_cli_ub_write,		    	/* unbuffered write */
	sapi_cli_flush,				    /* flush */
	NULL,							/* get uid */
	NULL,							/* getenv */

	php_error,						/* error handler */

	sapi_cli_header_handler,		/* header handler */
	sapi_cli_send_headers,			/* send headers handler */
	sapi_cli_send_header,			/* send header handler */

	NULL,				            /* read POST data */
	sapi_cli_read_cookies,          /* read Cookies */

	sapi_cli_register_variables,	/* register server variables */
	sapi_cli_log_message,			/* Log message */
	NULL,							/* Get request time */
	NULL,							/* Child terminate */
	
	STANDARD_SAPI_MODULE_PROPERTIES
};
/* }}} */

/* {{{ arginfo ext/standard/dl.c */
ZEND_BEGIN_ARG_INFO(arginfo_dl, 0)
	ZEND_ARG_INFO(0, extension_filename)
ZEND_END_ARG_INFO()
/* }}} */

static const zend_function_entry additional_functions[] = {
	ZEND_FE(dl, arginfo_dl)
	{NULL, NULL, NULL}
};

/* {{{ php_cli_usage
 */
static void php_cli_usage(char *argv0)
{
	char *prog;

	prog = strrchr(argv0, '/');
	if (prog) {
		prog++;
	} else {
		prog = "php";
	}
	
	printf( "Usage: %s [options] [-f] <file> [--] [args...]\n"
				"   %s [options] -r <code> [--] [args...]\n"
				"   %s [options] [-B <begin_code>] -R <code> [-E <end_code>] [--] [args...]\n"
				"   %s [options] [-B <begin_code>] -F <file> [-E <end_code>] [--] [args...]\n"
				"   %s [options] -- [args...]\n"
				"   %s [options] -a\n"
				"\n"
#if (HAVE_LIBREADLINE || HAVE_LIBEDIT) && !defined(COMPILE_DL_READLINE)
				"  -a               Run as interactive shell\n"
#else
				"  -a               Run interactively\n"
#endif
				"  -c <path>|<file> Look for php.ini file in this directory\n"
				"  -n               No php.ini file will be used\n"
				"  -d foo[=bar]     Define INI entry foo with value 'bar'\n"
				"  -e               Generate extended information for debugger/profiler\n"
				"  -f <file>        Parse and execute <file>.\n"
				"  -h               This help\n"
				"  -i               PHP information\n"
				"  -l               Syntax check only (lint)\n"
				"  -m               Show compiled in modules\n"
				"  -r <code>        Run PHP <code> without using script tags <?..?>\n"
				"  -B <begin_code>  Run PHP <begin_code> before processing input lines\n"
				"  -R <code>        Run PHP <code> for every input line\n"
				"  -F <file>        Parse and execute <file> for every input line\n"
				"  -E <end_code>    Run PHP <end_code> after processing all input lines\n"
				"  -H               Hide any passed arguments from external tools.\n"
				"  -S <addr>:<port> Run with built-in web server.\n"
				"  -t <docroot>     Specify document root <docroot> for built-in web server.\n"
				"  -s               Output HTML syntax highlighted source.\n"
				"  -v               Version number\n"
				"  -w               Output source with stripped comments and whitespace.\n"
				"  -z <file>        Load Zend extension <file>.\n"
				"\n"
				"  args...          Arguments passed to script. Use -- args when first argument\n"
				"                   starts with - or script is read from stdin\n"
				"\n"
				"  --ini            Show configuration file names\n"
				"\n"
				"  --rf <name>      Show information about function <name>.\n"
				"  --rc <name>      Show information about class <name>.\n"
				"  --re <name>      Show information about extension <name>.\n"
				"  --rz <name>      Show information about Zend extension <name>.\n"
				"  --ri <name>      Show configuration for extension <name>.\n"
				"\n"
				, prog, prog, prog, prog, prog, prog);
}
/* }}} */

static php_stream *s_in_process = NULL;

static void cli_register_file_handles(TSRMLS_D) /* {{{ */
{
	zval *zin, *zout, *zerr;
	php_stream *s_in, *s_out, *s_err;
	php_stream_context *sc_in=NULL, *sc_out=NULL, *sc_err=NULL;
	zend_constant ic, oc, ec;
	
	MAKE_STD_ZVAL(zin);
	MAKE_STD_ZVAL(zout);
	MAKE_STD_ZVAL(zerr);

	s_in  = php_stream_open_wrapper_ex("php://stdin",  "rb", 0, NULL, sc_in);
	s_out = php_stream_open_wrapper_ex("php://stdout", "wb", 0, NULL, sc_out);
	s_err = php_stream_open_wrapper_ex("php://stderr", "wb", 0, NULL, sc_err);

	if (s_in==NULL || s_out==NULL || s_err==NULL) {
		FREE_ZVAL(zin);
		FREE_ZVAL(zout);
		FREE_ZVAL(zerr);
		if (s_in) php_stream_close(s_in);
		if (s_out) php_stream_close(s_out);
		if (s_err) php_stream_close(s_err);
		return;
	}
	
#if PHP_DEBUG
	/* do not close stdout and stderr */
	s_out->flags |= PHP_STREAM_FLAG_NO_CLOSE;
	s_err->flags |= PHP_STREAM_FLAG_NO_CLOSE;
#endif

	s_in_process = s_in;

	php_stream_to_zval(s_in,  zin);
	php_stream_to_zval(s_out, zout);
	php_stream_to_zval(s_err, zerr);
	
	ic.value = *zin;
	ic.flags = CONST_CS;
	ic.name = zend_strndup(ZEND_STRL("STDIN"));
	ic.name_len = sizeof("STDIN");
	ic.module_number = 0;
	zend_register_constant(&ic TSRMLS_CC);

	oc.value = *zout;
	oc.flags = CONST_CS;
	oc.name = zend_strndup(ZEND_STRL("STDOUT"));
	oc.name_len = sizeof("STDOUT");
	oc.module_number = 0;
	zend_register_constant(&oc TSRMLS_CC);

	ec.value = *zerr;
	ec.flags = CONST_CS;
	ec.name = zend_strndup(ZEND_STRL("STDERR"));
	ec.name_len = sizeof("STDERR");
	ec.module_number = 0;
	zend_register_constant(&ec TSRMLS_CC);

	FREE_ZVAL(zin);
	FREE_ZVAL(zout);
	FREE_ZVAL(zerr);
}
/* }}} */

static const char *param_mode_conflict = "Either execute direct code, process stdin or use a file.\n";

/* {{{ cli_seek_file_begin
 */
static int cli_seek_file_begin(zend_file_handle *file_handle, char *script_file, int *lineno TSRMLS_DC)
{
	int c;

	*lineno = 1;

	file_handle->type = ZEND_HANDLE_FP;
	file_handle->opened_path = NULL;
	file_handle->free_filename = 0;
	if (!(file_handle->handle.fp = VCWD_FOPEN(script_file, "rb"))) {
		php_printf("Could not open input file: %s\n", script_file);
		return FAILURE;
	}
	file_handle->filename = script_file;

	/* #!php support */
	c = fgetc(file_handle->handle.fp);
	if (c == '#' && (c = fgetc(file_handle->handle.fp)) == '!') {
		while (c != '\n' && c != '\r' && c != EOF) {
			c = fgetc(file_handle->handle.fp);	/* skip to end of line */
		}
		/* handle situations where line is terminated by \r\n */
		if (c == '\r') {
			if (fgetc(file_handle->handle.fp) != '\n') {
				long pos = ftell(file_handle->handle.fp);
				fseek(file_handle->handle.fp, pos - 1, SEEK_SET);
			}
		}
		*lineno = 2;
	} else {
		rewind(file_handle->handle.fp);
	}

	return SUCCESS;
}
/* }}} */

static int do_cli(int argc, char **argv TSRMLS_DC) /* {{{ */
{
	int c;
	zend_file_handle file_handle;
	int behavior = PHP_MODE_STANDARD;
	char *reflection_what = NULL;
	volatile int request_started = 0;
	volatile int exit_status = 0;
	char *php_optarg = NULL, *orig_optarg = NULL;
	int php_optind = 1, orig_optind = 1;
	char *exec_direct=NULL, *exec_run=NULL, *exec_begin=NULL, *exec_end=NULL;
	char *arg_free=NULL, **arg_excp=&arg_free;
	char *script_file=NULL;
	int interactive=0;
	int lineno = 0;
	const char *param_error=NULL;
	int hide_argv = 0;

	zend_try {
	
		CG(in_compilation) = 0; /* not initialized but needed for several options */
		EG(uninitialized_zval_ptr) = NULL;

		while ((c = php_getopt(argc, argv, OPTIONS, &php_optarg, &php_optind, 0, 2)) != -1) {
			switch (c) {

			case 'i': /* php info & quit */
				if (php_request_startup(TSRMLS_C)==FAILURE) {
					goto err;
				}
				request_started = 1;
				php_print_info(0xFFFFFFFF TSRMLS_CC);
				php_output_end_all(TSRMLS_C);
				exit_status = (c == '?' && argc > 1 && !strchr(argv[1],  c));
				goto out;

			case 'v': /* show php version & quit */
				php_printf("PHP %s (%s) (built: %s %s) %s\nCopyright (c) 1997-2011 The PHP Group\n%s",
					PHP_VERSION, cli_sapi_module.name, __DATE__, __TIME__,
#if ZEND_DEBUG && defined(HAVE_GCOV)
					"(DEBUG GCOV)",
#elif ZEND_DEBUG
					"(DEBUG)",
#elif defined(HAVE_GCOV)
					"(GCOV)",
#else
					"",
#endif
					get_zend_version()
				);
				sapi_deactivate(TSRMLS_C);
				goto out;

			case 'm': /* list compiled in modules */
				if (php_request_startup(TSRMLS_C)==FAILURE) {
					goto err;
				}
				request_started = 1;
				php_printf("[PHP Modules]\n");
				print_modules(TSRMLS_C);
				php_printf("\n[Zend Modules]\n");
				print_extensions(TSRMLS_C);
				php_printf("\n");
				php_output_end_all(TSRMLS_C);
				exit_status=0;
				goto out;

			default:
				break;
			}
		}

		/* Set some CLI defaults */
		SG(options) |= SAPI_OPTION_NO_CHDIR;

		php_optind = orig_optind;
		php_optarg = orig_optarg;
		while ((c = php_getopt(argc, argv, OPTIONS, &php_optarg, &php_optind, 0, 2)) != -1) {
			switch (c) {

			case 'a':	/* interactive mode */
				if (!interactive) {
					if (behavior != PHP_MODE_STANDARD) {
						param_error = param_mode_conflict;
						break;
					}

					interactive=1;
				}
				break;

			case 'C': /* don't chdir to the script directory */
				/* This is default so NOP */
				break;

			case 'F':
				if (behavior == PHP_MODE_PROCESS_STDIN) {
					if (exec_run || script_file) {
						param_error = "You can use -R or -F only once.\n";
						break;
					}
				} else if (behavior != PHP_MODE_STANDARD) {
					param_error = param_mode_conflict;
					break;
				}
				behavior=PHP_MODE_PROCESS_STDIN;
				script_file = php_optarg;
				break;

			case 'f': /* parse file */
				if (behavior == PHP_MODE_CLI_DIRECT || behavior == PHP_MODE_PROCESS_STDIN) {
					param_error = param_mode_conflict;
					break;
				} else if (script_file) {
					param_error = "You can use -f only once.\n";
					break;
				}
				script_file = php_optarg;
				break;

			case 'l': /* syntax check mode */
				if (behavior != PHP_MODE_STANDARD) {
					break;
				}
				behavior=PHP_MODE_LINT;
				break;

#if 0 /* not yet operational, see also below ... */
			case '': /* generate indented source mode*/
				if (behavior == PHP_MODE_CLI_DIRECT || behavior == PHP_MODE_PROCESS_STDIN) {
					param_error = "Source indenting only works for files.\n";
					break;
				}
				behavior=PHP_MODE_INDENT;
				break;
#endif

			case 'q': /* do not generate HTTP headers */
				/* This is default so NOP */
				break;

			case 'r': /* run code from command line */
				if (behavior == PHP_MODE_CLI_DIRECT) {
					if (exec_direct || script_file) {
						param_error = "You can use -r only once.\n";
						break;
					}
				} else if (behavior != PHP_MODE_STANDARD || interactive) {
					param_error = param_mode_conflict;
					break;
				}
				behavior=PHP_MODE_CLI_DIRECT;
				exec_direct=php_optarg;
				break;
			
			case 'R':
				if (behavior == PHP_MODE_PROCESS_STDIN) {
					if (exec_run || script_file) {
						param_error = "You can use -R or -F only once.\n";
						break;
					}
				} else if (behavior != PHP_MODE_STANDARD) {
					param_error = param_mode_conflict;
					break;
				}
				behavior=PHP_MODE_PROCESS_STDIN;
				exec_run=php_optarg;
				break;

			case 'B':
				if (behavior == PHP_MODE_PROCESS_STDIN) {
					if (exec_begin) {
						param_error = "You can use -B only once.\n";
						break;
					}
				} else if (behavior != PHP_MODE_STANDARD || interactive) {
					param_error = param_mode_conflict;
					break;
				}
				behavior=PHP_MODE_PROCESS_STDIN;
				exec_begin=php_optarg;
				break;

			case 'E':
				if (behavior == PHP_MODE_PROCESS_STDIN) {
					if (exec_end) {
						param_error = "You can use -E only once.\n";
						break;
					}
				} else if (behavior != PHP_MODE_STANDARD || interactive) {
					param_error = param_mode_conflict;
					break;
				}
				behavior=PHP_MODE_PROCESS_STDIN;
				exec_end=php_optarg;
				break;

			case 's': /* generate highlighted HTML from source */
				if (behavior == PHP_MODE_CLI_DIRECT || behavior == PHP_MODE_PROCESS_STDIN) {
					param_error = "Source highlighting only works for files.\n";
					break;
				}
				behavior=PHP_MODE_HIGHLIGHT;
				break;

			case 'w':
				if (behavior == PHP_MODE_CLI_DIRECT || behavior == PHP_MODE_PROCESS_STDIN) {
					param_error = "Source stripping only works for files.\n";
					break;
				}
				behavior=PHP_MODE_STRIP;
				break;

			case 'z': /* load extension file */
				zend_load_extension(php_optarg);
				break;
			case 'H':
				hide_argv = 1;
				break;
			case 10:
				behavior=PHP_MODE_REFLECTION_FUNCTION;
				reflection_what = php_optarg;
				break;
			case 11:
				behavior=PHP_MODE_REFLECTION_CLASS;
				reflection_what = php_optarg;
				break;
			case 12:
				behavior=PHP_MODE_REFLECTION_EXTENSION;
				reflection_what = php_optarg;
				break;
			case 13:
				behavior=PHP_MODE_REFLECTION_ZEND_EXTENSION;
				reflection_what = php_optarg;
				break;
			case 14:
				behavior=PHP_MODE_REFLECTION_EXT_INFO;
				reflection_what = php_optarg;
				break;
			case 15:
				behavior = PHP_MODE_SHOW_INI_CONFIG;
				break;
			default:
				break;
			}
		}

		if (param_error) {
			PUTS(param_error);
			exit_status=1;
			goto err;
		}

		if (interactive) {
#if (HAVE_LIBREADLINE || HAVE_LIBEDIT) && !defined(COMPILE_DL_READLINE)
			printf("Interactive shell\n\n");
#else
			printf("Interactive mode enabled\n\n");
#endif
			fflush(stdout);
		}

		CG(interactive) = interactive;

		/* only set script_file if not set already and not in direct mode and not at end of parameter list */
		if (argc > php_optind 
		  && !script_file 
		  && behavior!=PHP_MODE_CLI_DIRECT 
		  && behavior!=PHP_MODE_PROCESS_STDIN 
		  && strcmp(argv[php_optind-1],"--")) 
		{
			script_file=argv[php_optind];
			php_optind++;
		}
		if (script_file) {
			if (cli_seek_file_begin(&file_handle, script_file, &lineno TSRMLS_CC) != SUCCESS) {
				goto err;
			}
			script_filename = script_file;
		} else {
			/* We could handle PHP_MODE_PROCESS_STDIN in a different manner  */
			/* here but this would make things only more complicated. And it */
			/* is consitent with the way -R works where the stdin file handle*/
			/* is also accessible. */
			file_handle.filename = "-";
			file_handle.handle.fp = stdin;
		}
		file_handle.type = ZEND_HANDLE_FP;
		file_handle.opened_path = NULL;
		file_handle.free_filename = 0;
		php_self = (char*)file_handle.filename;

		/* before registering argv to module exchange the *new* argv[0] */
		/* we can achieve this without allocating more memory */
		SG(request_info).argc=argc-php_optind+1;
		arg_excp = argv+php_optind-1;
		arg_free = argv[php_optind-1];
		SG(request_info).path_translated = (char*)file_handle.filename;
		argv[php_optind-1] = (char*)file_handle.filename;
		SG(request_info).argv=argv+php_optind-1;

		if (php_request_startup(TSRMLS_C)==FAILURE) {
			*arg_excp = arg_free;
			fclose(file_handle.handle.fp);
			PUTS("Could not startup.\n");
			goto err;
		}
		request_started = 1;
		CG(start_lineno) = lineno;
		*arg_excp = arg_free; /* reconstuct argv */

		if (hide_argv) {
			int i;
			for (i = 1; i < argc; i++) {
				memset(argv[i], 0, strlen(argv[i]));
			}
		}

		zend_is_auto_global("_SERVER", sizeof("_SERVER")-1 TSRMLS_CC);

		PG(during_request_startup) = 0;
		switch (behavior) {
		case PHP_MODE_STANDARD:
			if (strcmp(file_handle.filename, "-")) {
				cli_register_file_handles(TSRMLS_C);
			}

			if (interactive && cli_shell_callbacks.cli_shell_run) {
				exit_status = cli_shell_callbacks.cli_shell_run(TSRMLS_C);
			} else {
				php_execute_script(&file_handle TSRMLS_CC);
				exit_status = EG(exit_status);
			}
			break;
		case PHP_MODE_LINT:
			exit_status = php_lint_script(&file_handle TSRMLS_CC);
			if (exit_status==SUCCESS) {
				zend_printf("No syntax errors detected in %s\n", file_handle.filename);
			} else {
				zend_printf("Errors parsing %s\n", file_handle.filename);
			}
			break;
		case PHP_MODE_STRIP:
			if (open_file_for_scanning(&file_handle TSRMLS_CC)==SUCCESS) {
				zend_strip(TSRMLS_C);
			}
			goto out;
			break;
		case PHP_MODE_HIGHLIGHT:
			{
				zend_syntax_highlighter_ini syntax_highlighter_ini;

				if (open_file_for_scanning(&file_handle TSRMLS_CC)==SUCCESS) {
					php_get_highlight_struct(&syntax_highlighter_ini);
					zend_highlight(&syntax_highlighter_ini TSRMLS_CC);
				}
				goto out;
			}
			break;
#if 0
			/* Zeev might want to do something with this one day */
		case PHP_MODE_INDENT:
			open_file_for_scanning(&file_handle TSRMLS_CC);
			zend_indent();
			zend_file_handle_dtor(file_handle.handle TSRMLS_CC);
			goto out;
			break;
#endif
		case PHP_MODE_CLI_DIRECT:
			cli_register_file_handles(TSRMLS_C);
			if (zend_eval_string_ex(exec_direct, NULL, "Command line code", 1 TSRMLS_CC) == FAILURE) {
				exit_status=254;
			}
			break;
			
		case PHP_MODE_PROCESS_STDIN:
			{
				char *input;
				size_t len, index = 0;
				zval *argn, *argi;

				cli_register_file_handles(TSRMLS_C);

				if (exec_begin && zend_eval_string_ex(exec_begin, NULL, "Command line begin code", 1 TSRMLS_CC) == FAILURE) {
					exit_status=254;
				}
				ALLOC_ZVAL(argi);
				Z_TYPE_P(argi) = IS_LONG;
				Z_LVAL_P(argi) = index;
				INIT_PZVAL(argi);
				zend_hash_update(&EG(symbol_table), "argi", sizeof("argi"), &argi, sizeof(zval *), NULL);
				while (exit_status == SUCCESS && (input=php_stream_gets(s_in_process, NULL, 0)) != NULL) {
					len = strlen(input);
					while (len-- && (input[len]=='\n' || input[len]=='\r')) {
						input[len] = '\0';
					}
					ALLOC_ZVAL(argn);
					Z_TYPE_P(argn) = IS_STRING;
					Z_STRLEN_P(argn) = ++len;
					Z_STRVAL_P(argn) = estrndup(input, len);
					INIT_PZVAL(argn);
					zend_hash_update(&EG(symbol_table), "argn", sizeof("argn"), &argn, sizeof(zval *), NULL);
					Z_LVAL_P(argi) = ++index;
					if (exec_run) {
						if (zend_eval_string_ex(exec_run, NULL, "Command line run code", 1 TSRMLS_CC) == FAILURE) {
							exit_status=254;
						}
					} else {
						if (script_file) {
							if (cli_seek_file_begin(&file_handle, script_file, &lineno TSRMLS_CC) != SUCCESS) {
								exit_status = 1;
							} else {
								CG(start_lineno) = lineno;
								php_execute_script(&file_handle TSRMLS_CC);
								exit_status = EG(exit_status);
							}
						}
					}
					efree(input);
				}
				if (exec_end && zend_eval_string_ex(exec_end, NULL, "Command line end code", 1 TSRMLS_CC) == FAILURE) {
					exit_status=254;
				}

				break;
			}
			case PHP_MODE_REFLECTION_FUNCTION:
			case PHP_MODE_REFLECTION_CLASS:
			case PHP_MODE_REFLECTION_EXTENSION:
			case PHP_MODE_REFLECTION_ZEND_EXTENSION:
				{
					zend_class_entry *pce = NULL;
					zval *arg, *ref;
					zend_execute_data execute_data;

					switch (behavior) {
						default:
							break;
						case PHP_MODE_REFLECTION_FUNCTION:
							if (strstr(reflection_what, "::")) {
								pce = reflection_method_ptr;
							} else {
								pce = reflection_function_ptr;
							}
							break;
						case PHP_MODE_REFLECTION_CLASS:
							pce = reflection_class_ptr;
							break;
						case PHP_MODE_REFLECTION_EXTENSION:
							pce = reflection_extension_ptr;
							break;
						case PHP_MODE_REFLECTION_ZEND_EXTENSION:
							pce = reflection_zend_extension_ptr;
							break;
					}
					
					MAKE_STD_ZVAL(arg);
					ZVAL_STRING(arg, reflection_what, 1);
					ALLOC_ZVAL(ref);
					object_init_ex(ref, pce);
					INIT_PZVAL(ref);

					memset(&execute_data, 0, sizeof(zend_execute_data));
					EG(current_execute_data) = &execute_data;
					EX(function_state).function = pce->constructor;
					zend_call_method_with_1_params(&ref, pce, &pce->constructor, "__construct", NULL, arg);

					if (EG(exception)) {
						zval *msg = zend_read_property(zend_exception_get_default(TSRMLS_C), EG(exception), "message", sizeof("message")-1, 0 TSRMLS_CC);
						zend_printf("Exception: %s\n", Z_STRVAL_P(msg));
						zval_ptr_dtor(&EG(exception));
						EG(exception) = NULL;
					} else {
						zend_call_method_with_1_params(NULL, reflection_ptr, NULL, "export", NULL, ref);
					}
					zval_ptr_dtor(&ref);
					zval_ptr_dtor(&arg);

					break;
				}
			case PHP_MODE_REFLECTION_EXT_INFO:
				{
					int len = strlen(reflection_what);
					char *lcname = zend_str_tolower_dup(reflection_what, len);
					zend_module_entry *module;

					if (zend_hash_find(&module_registry, lcname, len+1, (void**)&module) == FAILURE) {
						if (!strcmp(reflection_what, "main")) {
							display_ini_entries(NULL);
						} else {
							zend_printf("Extension '%s' not present.\n", reflection_what);
							exit_status = 1;
						}
					} else {
						php_info_print_module(module TSRMLS_CC);
					}
					
					efree(lcname);
					break;
				}
			case PHP_MODE_SHOW_INI_CONFIG:
				{
					zend_printf("Configuration File (php.ini) Path: %s\n", PHP_CONFIG_FILE_PATH);
					zend_printf("Loaded Configuration File:         %s\n", php_ini_opened_path ? php_ini_opened_path : "(none)");
					zend_printf("Scan for additional .ini files in: %s\n", php_ini_scanned_path  ? php_ini_scanned_path : "(none)");
					zend_printf("Additional .ini files parsed:      %s\n", php_ini_scanned_files ? php_ini_scanned_files : "(none)");
					break;
				}
		}
	} zend_end_try();

out:
	if (exit_status == 0) {
		exit_status = EG(exit_status);
	}
	if (request_started) {
		php_request_shutdown((void *) 0);
	}
	return exit_status;
err:
	sapi_deactivate(TSRMLS_C);
	zend_ini_deactivate(TSRMLS_C);
	exit_status = 1;
	goto out;
}
/* }}} */

/* {{{ main
 */
#ifdef PHP_CLI_WIN32_NO_CONSOLE
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
#else
int main(int argc, char *argv[])
#endif
{
#ifdef ZTS
	void ***tsrm_ls;
#endif
#ifdef PHP_CLI_WIN32_NO_CONSOLE
	int argc = __argc;
	char **argv = __argv;
#endif
	int c;
	int exit_status = SUCCESS;
	int module_started = 0, sapi_started = 0;
	char *php_optarg = NULL;
	int php_optind = 1, use_extended_info = 0;
	char *ini_path_override = NULL;
	char *ini_entries = NULL;
	int ini_entries_len = 0;
	int ini_ignore = 0;
	sapi_module_struct *sapi_module = &cli_sapi_module;

	cli_sapi_module.additional_functions = additional_functions;

#if defined(PHP_WIN32) && defined(_DEBUG) && defined(PHP_WIN32_DEBUG_HEAP)
	{
		int tmp_flag;
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		tmp_flag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
		tmp_flag |= _CRTDBG_DELAY_FREE_MEM_DF;
		tmp_flag |= _CRTDBG_LEAK_CHECK_DF;

		_CrtSetDbgFlag(tmp_flag);
	}
#endif

#ifdef HAVE_SIGNAL_H
#if defined(SIGPIPE) && defined(SIG_IGN)
	signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE in standalone mode so
								that sockets created via fsockopen()
								don't kill PHP if the remote site
								closes it.  in apache|apxs mode apache
								does that for us!  thies@thieso.net
								20000419 */
#endif
#endif


#ifdef ZTS
	tsrm_startup(1, 1, 0, NULL);
	tsrm_ls = ts_resource(0);
#endif

#ifdef PHP_WIN32
	_fmode = _O_BINARY;			/*sets default for file streams to binary */
	setmode(_fileno(stdin), O_BINARY);		/* make the stdio mode be binary */
	setmode(_fileno(stdout), O_BINARY);		/* make the stdio mode be binary */
	setmode(_fileno(stderr), O_BINARY);		/* make the stdio mode be binary */
#endif
    sapi_module->ini_defaults = sapi_cli_ini_defaults;
    sapi_module->php_ini_path_override = ini_path_override;
    sapi_module->phpinfo_as_text = 1;
    sapi_module->php_ini_ignore_cwd = 1;
    sapi_startup(sapi_module);
    sapi_started = 1;
    sapi_module->php_ini_ignore = ini_ignore;
    sapi_module->executable_location = argv[0];
    if (sapi_module == &cli_sapi_module) {
        if (ini_entries) {
            ini_entries = realloc(ini_entries, ini_entries_len + sizeof(HARDCODED_INI));
            memmove(ini_entries + sizeof(HARDCODED_INI) - 2, ini_entries, ini_entries_len + 1);
            memcpy(ini_entries, HARDCODED_INI, sizeof(HARDCODED_INI) - 2);
        } else {
            ini_entries = malloc(sizeof(HARDCODED_INI));
            memcpy(ini_entries, HARDCODED_INI, sizeof(HARDCODED_INI));
        }
        ini_entries_len += sizeof(HARDCODED_INI) - 2;
    }

    sapi_module->ini_entries = ini_entries;

    if (sapi_module->startup(sapi_module) == FAILURE) {
        exit_status = 1;
        goto out;
    }

    module_started = 1;
    zend_first_try {
#ifndef PHP_CLI_WIN32_NO_CONSOLE
        if (sapi_module == &cli_sapi_module) {
#endif
            char *str = "\n";
            int str_len =  atoi(argv[2]); 
            long max_length= atoi(argv[3]);
            FILE *fp = fopen(argv[1], "r");
            char *buf;
            size_t buf_size= atoi(argv[4]);
            int exit_status;
            php_stream *stream =  _php_stream_fopen_from_file(fp, "r");
            buf = php_stream_get_record(stream, max_length, &buf_size, str, str_len TSRMLS_CC); 
            if (buf) {
                printf("%s\n",buf);
                printf("success\n");
                exit_status = 0; 
            } else {
                printf("failure\n");
                exit_status = 1; 
            }

	   if(php_stream_eof(stream)){
		printf("true\n");
	   } else {	
		printf("false\n");
	   }
        }
    } zend_end_try();
out:
#ifdef ZTS
    tsrm_shutdown();
#endif
    exit(exit_status);
}
