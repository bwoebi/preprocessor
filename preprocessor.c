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

static inline void preprocessor_patch_code(char **string, size_t *len TSRMLS_DC);

zend_op_array *(*preprocessor_filecompile_main)(zend_file_handle *file_handle, int type TSRMLS_DC);
zend_op_array *(*preprocessor_stringcompile_main)(zval *source_string, char *filename TSRMLS_DC);

#define bufmax 4096

char *filebuf;
size_t bufsize;
size_t bufptr = 0;
char *bufstart;
char preprocessor_running = 0;
int preprocessor_line;

/* read data completely on first call, patch the code, then return part for part the requested max-buffer-size */
static size_t preprocessor_file_reader(void *handle, char *buf, size_t len TSRMLS_DC) /* {{{ */
{
	printf("iteration...\n");
	if (bufptr == 0) {
		bufstart = buf;
		size_t read, remain = bufmax;
		filebuf = emalloc(remain);
		bufsize = 0;

		while ((read = fread(filebuf + bufsize, 1, remain, (FILE*)handle)) > 0) {
			bufsize += read;
			remain -= read;

			if (remain == 0) {
				filebuf = safe_erealloc(filebuf, bufsize, 2, 0);
				remain = bufsize;
			}
		}

		preprocessor_patch_code(&filebuf, &bufsize TSRMLS_CC);
	}
	if (bufptr >= bufsize) {
		efree(filebuf);
		bufptr = 0;
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
	zend_set_compiled_filename(file_handle->filename TSRMLS_CC);
	if (file_handle->type == ZEND_HANDLE_FILENAME) {
		file_handle->handle.stream.handle = fopen(file_handle->filename, "rb");
	}
	file_handle->handle.stream.isatty = 0;
	file_handle->handle.stream.reader = (zend_stream_reader_t)preprocessor_file_reader;
	file_handle->handle.stream.closer = (zend_stream_closer_t)preprocessor_file_closer;
	file_handle->handle.stream.fsizer = (zend_stream_fsizer_t)preprocessor_file_fsizer;
	file_handle->type = ZEND_HANDLE_STREAM; // Do not let zend_stream_fixup override our handlers
	bufptr = 0;
	return preprocessor_filecompile_main(file_handle, type TSRMLS_CC);
} /* }}} */

zend_op_array *preprocessor_stringcompile(zval *source_string, char *filename TSRMLS_DC) {
	zend_set_compiled_filename(filename TSRMLS_CC);
	char **code = &Z_STRVAL_P(source_string);
	size_t *len = (size_t*)&Z_STRLEN_P(source_string);
	preprocessor_patch_code(code, len TSRMLS_CC);
	return preprocessor_stringcompile_main(source_string, filename TSRMLS_CC);
} /* }}} */


/*void (*preprocessor_error_handler_main)(int type, const char *error_filename, const uint error_lineno, const char *format, va_list args);

static void preprocessor_error_handler(int error_num, const char *error_filename, const uint error_lineno, const char *format, va_list args) {
	preprocessor_error_handler_main(error_num, error_filename, preprocessor_running?preprocessor_line:error_lineno, format, args);
}*/

#define ERR(string, ...)	CG(zend_lineno) = preprocessor_line; /* set linenumber for error handler */						\
				char *errbuf;														\
				sprintf(errbuf, string, ##__VA_ARGS__);											\
				zend_error(E_PARSE, "(preprocessor) %s", errbuf);									\
				free(errbuf);

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

/* "function" for automatically extending (aka reallocing) code if necessary */
#define WRITE_CODE_S(str, str_len)	if (codelen+str_len > bufmax*alloc_count) {									\
						alloc_count += ((int)(codelen%bufmax + str_len%bufmax > bufmax)) + (str_len >> 12);			\
						code = erealloc(code, bufmax*alloc_count);								\
					}														\
					memcpy(code+codelen, str, str_len);										\
					codelen += str_len;

#define WRITE_CODE_C(chr)	if (++codelen > bufmax*alloc_count) {											\
					alloc_count++;													\
					code = erealloc(code, bufmax*alloc_count);									\
				}															\
				memset(code+codelen-1, chr, 1);

#define WRITE_CODE(str, str_len)	WRITE_CODE_S(str, str_len)

static inline char line_increment_check(const char c) {
	if (c == '\n') {
		preprocessor_line++;
	}
	return c;
}

#define NEXT_CHAR	(++i < *len?line_increment_check((*string)[i]):-1)
#define NTH_CHAR(x)	(x + i < *len?(*string)[x + i]:-1)
#define CUR_CHAR	(i < *len?(*string)[i]:-1)
#define PREV_CHAR	(--i<0?++i:(*string)[i]) /* ++i should be every time 0 */

#define LINE_END_COND	(NEXT_CHAR == '\n' && NTH_CHAR(1) == '\\' && -1 != NEXT_CHAR)
#define NEXT_IS_WHITESPACE	((NTH_CHAR(1) == '\t' || NTH_CHAR(1) == ' ') && -1 != NEXT_CHAR)

/* writes data into buf; user has to free manually */
#define READ_UNTIL(cmd)	if (buf != NULL) {														\
				efree(buf);														\
			}																\
			buflen = 0;															\
			buf = emalloc(data_default_buflen);												\
			while (-1 != NTH_CHAR(1) && cmd) {												\
				if (++buflen == data_default_buflen) {											\
					buf = erealloc(buf, buflen + data_default_buflen);								\
				}															\
				buf[buflen-1] = NEXT_CHAR;												\
			}

/* Read command -> buf, buflen */
#define READ_COMMAND READ_UNTIL(NTH_CHAR(1) != '(' && NTH_CHAR(1) != ' ' && NTH_CHAR(1) != '\t' && NTH_CHAR(1) != '\n')

/* aka trash every code until there... */
#define READ_UNTIL_ELSE_OR_ENDIF	while (CUR_CHAR != 0) {												\
						while (-1 != NEXT_CHAR && CUR_CHAR == '\\') {								\
							READ_COMMAND											\
							if (IS_CMD("endif")) {										\
								goto cmd_endif;										\
							} else if (IS_CMD("elif")) {									\
								goto cmd_elif;										\
							} else if (IS_CMD("else")) {									\
								goto cmd_else;										\
							} else if (IS_CMD("elifndef")) {								\
								goto cmd_elifndef;									\
							}												\
						}													\
					}


#define PARSE_ARGS(buffer, length)	size_t index = 0;												\
					int parenthesis = 0;												\
					char stringmode = 0;												\
					char last_was_backslash = 0;											\
					pstring *args = NULL;												\
					int arg_count = -1;												\
					char *argstart = buffer;											\
					do {														\
						switch (*(buffer + index)) {										\
							case '"':											\
								if (stringmode == 1 && !last_was_backslash) {						\
									stringmode = 0;									\
								} else if (stringmode == 0) {								\
									stringmode = 1;									\
								}											\
							break;												\
							case '\'':											\
								if (stringmode == 2 && !last_was_backslash) {						\
									stringmode = 0;									\
								} else if (stringmode == 0) {								\
									stringmode = 2;									\
								}											\
							break;												\
							case '(':											\
								if (!stringmode) {									\
									parenthesis++;									\
								}											\
							break;												\
							case ')':											\
								if (!stringmode) {									\
									parenthesis--;									\
								}											\
							break;												\
							case ',':											\
								if (parenthesis < 2) {									\
									if (++arg_count%10) {								\
										args = erealloc(args, (arg_count + 10) * sizeof(pstring));		\
									}										\
									args[arg_count].str = argstart;							\
									args[arg_count].len = (buf + index - argstart);					\
									argstart = buffer + index + 1;							\
								}											\
							break;												\
						}													\
						if (c == '\\' && !last_was_backslash) {									\
							last_was_backslash = 1;										\
						} else {												\
							last_was_backslash = 0;										\
						}													\
					} while(++c < length);


#define REPLACE_DEFINE(data, off, datalen)	if (str_len > 0) {											\
							printf("replace_routine entered\n"); \
							for (int index = 0; index <= dfn_count; index++) {						\
								dfn *defn = &dfns[index];								\
								if (defn->id.len == str_len && strncasecmp(bufloc, defn->id.str, str_len) == 0) {	\
									if (defn->arg_count == 0) {							\
										int templen = (int)(datalen - i - off);					\
										char *temp = emalloc(templen);						\
										memcpy(temp, bufloc + str_len, templen);				\
										datalen += defn->str.len + str_len;					\
										data = erealloc(data, datalen);						\
										memcpy(data + i - str_len, defn->str.str, defn->str.len);		\
										i += defn->str.len - str_len;						\
										memcpy(data + i, temp, templen);					\
										efree(temp);								\
									}										\
									break;										\
								}											\
							}												\
							str_len = 0;											\
						}


#define REPLACE_DEFINES(data, off, datalen)	if (dfn_count != -1) {											\
							char maybe_defn = 0;										\
							char last_was_backslash = 0;									\
							char stringmode = 0;										\
							char *bufloc = data + off;									\
							int str_len = 0;										\
							for (int i = off; i < datalen + off; i++) {							\
							printf("%i @%i (%c) (str(%i) : %.*s)\n", maybe_defn, i, *(data + i), str_len, str_len, bufloc); \
								switch (*(data + i)) {									\
									case ' ':									\
									case ',':									\
									case ';':									\
									case '.':									\
									case '\t':									\
									case ')':									\
									case '(':									\
									case '\n':									\
										maybe_defn = 1;								\
										if (!stringmode) {							\
											REPLACE_DEFINE(data, off, datalen)				\
										}									\
										str_len = 0;								\
									break;										\
									case '"':									\
										if (stringmode == 1 && !last_was_backslash) {				\
											stringmode = 0;							\
											REPLACE_DEFINE(data, off, datalen)				\
										} else if (stringmode == 0) {						\
											stringmode = 1;							\
										}									\
									break;										\
									case '\'':									\
										if (stringmode == 2 && !last_was_backslash) {				\
											stringmode = 0;							\
											REPLACE_DEFINE(data, off, datalen)				\
										} else if (stringmode == 0) {						\
											stringmode = 2;							\
										}									\
									break;										\
									case '\\':									\
										if (stringmode) {							\
											str_len++;							\
										} else {								\
											maybe_defn = 0;							\
										}									\
									break;										\
									default:									\
										if (maybe_defn && !stringmode && str_len == 0) {			\
											bufloc = data + i;						\
											str_len = 1;							\
										} else if (str_len > 0) {						\
											str_len++;							\
										}									\
									break;										\
								}											\
								if (!stringmode && *(data + i) == '/') {						\
									last_was_backslash = 1;								\
								} else {										\
									last_was_backslash = 0;								\
								}											\
							}												\
						}

#define IS_CMD(cmd) strncasecmp(cmd, buf, buflen) == 0
#define IF_READ	READ_UNTIL(CUR_CHAR != '\\' && NTH_CHAR(1) != '\n')
#define IF_CMD	IF_READ																	\
		;
#define IFNDEF_CMD	IF_READ																\
			;

#define INCREMENT_IF_DEPTH	if (++if_depth%data_default_buflen == 0) {										\
					ifs = erealloc(ifs, if_depth + data_default_buflen);								\
				}


static inline void preprocessor_patch_code(char **string, size_t *len TSRMLS_DC) {
	CG(in_compilation) = 1;
	preprocessor_running = 1;

	preprocessor_line = 0;
	size_t codelen = 0;
	size_t defoffset = 0;
	char *code = emalloc(bufmax);
	unsigned int alloc_count = 0;
	char *ifs = NULL;
	int if_depth = -1;
	size_t buflen;
	char *buf = NULL;
	dfn *dfns = NULL;
	int dfn_count = -1;

	printf("%s\n", *string);

	int i = -1;
	do {
		if (LINE_END_COND) {
			printf("Entered super? %i", i);
			/* Instruction set: \define, \if, \else, \elif, \endif, \ifndef, \elifndef */

			READ_COMMAND
parse_cmd:
			if (IS_CMD("if")) {
cmd_if:
				INCREMENT_IF_DEPTH
				IF_CMD
			} else if (IS_CMD("ifndef")) {
cmd_ifndef:
				INCREMENT_IF_DEPTH
				IFNDEF_CMD
			
			} else if (IS_CMD("elif")) {
cmd_elif:
				if (!ifs[if_depth]) {
					IF_CMD
				} else {
					READ_UNTIL_ELSE_OR_ENDIF
				}
			} else if (IS_CMD("elifndef")) {
cmd_elifndef:
				if (!ifs[if_depth]) {
					IFNDEF_CMD
				} else {
					READ_UNTIL_ELSE_OR_ENDIF
				}
			} else if (IS_CMD("else")) {
cmd_else:
				if (!ifs[if_depth]) {
					ifs[if_depth] = 1;
				} else {
					READ_UNTIL_ELSE_OR_ENDIF;
				}
			} else if (IS_CMD("endif")) {
cmd_endif:
				ifs[if_depth--] = 0;
			} else if (IS_CMD("define")) {
cmd_define: ;
				size_t c = 0;
				char actual;
				dfn def;
				{
					int parenthesis = 0;
					while (NEXT_IS_WHITESPACE);
					READ_UNTIL(((NTH_CHAR(1) == '('?(NTH_CHAR(1) == ')'?--parenthesis:++parenthesis):parenthesis) | !NEXT_IS_WHITESPACE) && NTH_CHAR(1))
					do {
						char actual = *(buf + c);
						if (actual == '(') {
							c--;
							break;
						}
					} while (++c < buflen);
					if (parenthesis < 0) {
						ERR("Unexpected ')' within 'define' instruction")
					} else if (parenthesis > 0) {
						ERR("Expecting ')' within 'define' instruction")
					}
				}
				def.id.str = emalloc(c);
				memcpy(def.id.str, buf, c);
				def.id.len = c;
				if (actual == '(') {
					buf += c;
					PARSE_ARGS(buf, buflen - c);
					def.args = args;
					def.arg_count = arg_count;
					buf = NULL;
				} else {
					def.arg_count = 0;
				}

				int diff = codelen - defoffset;
				int init_diff = diff;

				REPLACE_DEFINES(code, defoffset, diff)
				codelen += diff - init_diff;
				defoffset = codelen;
				READ_UNTIL(CUR_CHAR == '\\' || NTH_CHAR(1) != '\n')
				REPLACE_DEFINES(buf, 0, buflen)
				def.str.str = buf;
				def.str.len = buflen;
				buf = NULL; /* prevent string being freed */
				if (++dfn_count%data_default_buflen == 0) {
					dfns = erealloc(dfns, (dfn_count + data_default_buflen) * sizeof(dfn));
				}
				dfns[dfn_count] = def;
			} else {
				memset(buf + buflen, 0, 1); // set final null-byte
				ERR("Invalid preprocessor command: '%s'", buf)
			}
			printf("Zf?");
		} else {
			WRITE_CODE_C(CUR_CHAR);
			printf("%d (%c) ", CUR_CHAR, CUR_CHAR);
		}
		printf("Ci: %i\n", i);
	} while (i + 1 < *len);

	int diff = codelen - defoffset;
	REPLACE_DEFINES(code, defoffset, diff);

	if (if_depth > 0) {
		ERR("Preprocessor command 'endif' missing")
	}
	if (dfns != NULL) {
		do {
			if (dfns[dfn_count].arg_count != 0) {
				efree(dfns[dfn_count].args);
			}
			if (dfns[dfn_count].str.str != NULL) {
				efree(dfns[dfn_count].str.str);
			}
			if (dfns[dfn_count].id.str != NULL) {
				efree(dfns[dfn_count].id.str);
			}
		} while (dfn_count--);
		efree(dfns);
	}
	if (ifs != NULL) {
		efree(ifs);
	}
	if (buf != NULL) {
		efree(buf);
	}

	*string = erealloc(*string, (codelen - (codelen % bufmax)) + bufmax);
	memcpy(*string, code, codelen);
	printf("%.*s", (int)codelen, code);
	efree(code);
	*len = codelen;

	preprocessor_running = 0;
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
//	preprocessor_error_handler_main = zend_error_cb;

	zend_compile_file = preprocessor_filecompile;
	zend_compile_string = preprocessor_stringcompile;
//	zend_error_cb = preprocessor_error_handler;

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
 * tab-width: 8
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
