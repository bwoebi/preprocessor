/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "Zend/zend_compile.h"
#include "Zend/zend_language_scanner.h"
#include "Zend/zend_language_scanner_defs.h"
#include "Zend/Zend_API.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_preprocessor.h"

/* If you declare any globals in php_preprocessor.h uncomment this: */
ZEND_DECLARE_MODULE_GLOBALS(preprocessor)

/* True global resources - no need for thread safety here */
static int le_preprocessor;

/* {{{ preprocessor_functions[]
 *
 * Every user visible function must have an entry in preprocessor_functions[].
 */
const zend_function_entry preprocessor_functions[] = {
	PHP_FE_END	/* Must be the last line in preprocessor_functions[] */
};
/* }}} */

/* {{{ preprocessor_module_entry
 */
zend_module_entry preprocessor_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"preprocessor",
	preprocessor_functions,
	PHP_MINIT(preprocessor),
	PHP_MSHUTDOWN(preprocessor),
	PHP_RINIT(preprocessor),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(preprocessor),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(preprocessor),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_PREPROCESSOR
ZEND_GET_MODULE(preprocessor)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("preprocessor.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_preprocessor_globals, preprocessor_globals)
    STD_PHP_INI_ENTRY("preprocessor.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_preprocessor_globals, preprocessor_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_preprocessor_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_preprocessor_init_globals(zend_preprocessor_globals *preprocessor_globals)
{
	preprocessor_globals->global_value = 0;
	preprocessor_globals->global_string = NULL;
}
*/
/* }}} */


/***/

void preprocessor_patch_code(char **string, size_t *len TSRMLS_DC);

zend_op_array *(*preprocessor_filecompile_main)(zend_file_handle *file_handle, int type TSRMLS_DC);
zend_op_array *(*preprocessor_stringcompile_main)(zval *source_string, char *filename TSRMLS_DC);

#define bufmax 4096

char *filebuf;
size_t bufsize;
size_t bufptr;
char *bufstart;

/* read data completely on first call, patch the code, then return part for part the requested max-buffer-size */
static size_t preprocessor_file_reader(void *handle, char *buf, size_t len TSRMLS_DC) /* {{{ */
{
	if (bufstart == NULL) {
		bufstart = buf;
		bufptr = 0;
		size_t read, remain = bufmax;
		filebuf = emalloc(remain);

		while ((read = fread(filebuf, 1, remain, (FILE*)handle)) > 0) {
			bufsize += read;
			remain -= read;

			if (remain == 0) {
				filebuf = safe_erealloc(filebuf, bufsize, 2, 0);
				remain = bufsize;
			}
		}

		preprocessor_patch_code(&bufstart, &bufsize TSRMLS_CC);
	}
	if (bufptr > bufsize) {
		efree(filebuf);
		return 0;
	}
	bufptr += len;
	size_t write = bufptr > bufsize?bufsize%bufmax:len;
	memcpy(buf, filebuf+bufptr-len, write);
	return write;
} /* }}} */

static void preprocessor_file_closer(void *handle TSRMLS_DC) /* {{{ */
{
	if (handle && (FILE*)handle != stdin) {
		fclose((FILE*)handle);
	}
} /* }}} */


static size_t preprocessor_file_fsizer(void *handle TSRMLS_DC) /* {{{ */
{
	return 0;
} /* }}} */

zend_op_array *preprocessor_filecompile(zend_file_handle *file_handle, int type TSRMLS_DC) {
	file_handle->handle.stream.isatty = 0;
	file_handle->handle.stream.reader = (zend_stream_reader_t)preprocessor_file_reader;
	file_handle->handle.stream.closer = (zend_stream_closer_t)preprocessor_file_closer;
	file_handle->handle.stream.fsizer = (zend_stream_fsizer_t)preprocessor_file_fsizer;
	file_handle->type = ZEND_HANDLE_STREAM; // Do not let zend_stream_fixup override our handlers
	bufstart = NULL;
	return preprocessor_filecompile_main(file_handle, type TSRMLS_CC);
} /* }}} */

zend_op_array *preprocessor_stringcompile(zval *source_string, char *filename TSRMLS_DC) {
	preprocessor_patch_code(&Z_STRVAL_P(source_string), (size_t*)&Z_STRLEN_P(source_string) TSRMLS_CC);
	return preprocessor_stringcompile_main(source_string, filename TSRMLS_CC);
} /* }}} */


void preprocessor_patch_code(char **string, size_t *len TSRMLS_DC) {
	printf("%s", *string);
} /* }}} */

/*|*/


/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(preprocessor)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/

	preprocessor_filecompile_main = zend_compile_file;
	preprocessor_stringcompile_main = zend_compile_string;

	zend_compile_file = preprocessor_filecompile;
	zend_compile_string = preprocessor_stringcompile;

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(preprocessor)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(preprocessor)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(preprocessor)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(preprocessor)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "Preprocessor support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


/* 
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
