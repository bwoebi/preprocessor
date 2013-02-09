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
  | Author: Bob Weinand <bobwei9@hotmail.com>                            |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

/* PHPPP => PHP_PreProcessor */

#ifndef PHPPP_H
#define PHPPP_H

extern zend_module_entry preprocessor_module_entry;
#define phpext_preprocessor_ptr &preprocessor_module_entry

#ifdef PHP_WIN32
#	define PHPPP_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHPPP_API __attribute__ ((visibility("default")))
#else
#	define PHPPP_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(preprocessor);
PHP_MSHUTDOWN_FUNCTION(preprocessor);
PHP_RINIT_FUNCTION(preprocessor);
PHP_RSHUTDOWN_FUNCTION(preprocessor);
PHP_MINFO_FUNCTION(preprocessor);

/*
  	Declare any global variables you may need between the BEGIN
	and END macros here:
*/
ZEND_BEGIN_MODULE_GLOBALS(preprocessor)
	char *filebuf;
	size_t bufsize;
	size_t bufptr;
	char running;
	int line;
ZEND_END_MODULE_GLOBALS(preprocessor)

/* In every utility function you add that needs to use variables
   in php_preprocessor_globals, call TSRMLS_FETCH(); after declaring other
   variables used by that function, or better yet, pass in TSRMLS_CC
   after the last function argument and declare your utility function
   with TSRMLS_DC after the last declared argument.  Always refer to
   the globals in your function as PHPPP_G(variable).
*/

#ifdef ZTS
#define PHPPP_G(v) TSRMG(preprocessor_globals_id, zend_preprocessor_globals *, v)
#else
#define PHPPP_G(v) (preprocessor_globals.v)
#endif

#define bufmax 4096
#define data_default_buflen 80

typedef struct preprocessor_string {
	char *str;
	int len;
} pstring;
typedef struct preprocessor_dfn {
	pstring id;
	pstring str;
	pstring *args;
	int arg_count;
} dfn;

#endif	/* PHPPP_H */


/*
 * Local variables:
 * tab-width: 8
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
