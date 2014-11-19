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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_webp.h"
#include "cwebp.h"

#include <unistd.h>
#include <fcntl.h>
#include <webp/encode.h>

#ifndef WEBP_DLL
#if defined(__cplusplus) || defined(c_plusplus)
extern "C" 
{
#endif

    extern void* VP8GetCPUInfo;   // opaque forward declaration.

#if defined(__cplusplus) || defined(c_plusplus)
}    // extern "C"
#endif
#endif  // WEBP_DLL

//------------------------------------------------------------------------------

/* If you declare any globals in php_webp.h uncomment this:
ZEND_DECLARE_MODULE_GLOBALS(webp)
*/

/* True global resources - no need for thread safety here */
static int le_webp;

/* {{{ webp_functions[]
 *
 * Every user visible function must have an entry in webp_functions[].
 */
const zend_function_entry webp_functions[] = {
	PHP_FE(image2webp,	NULL)		/* For testing, remove later. */
    {NULL,NULL,NULL}/* Must be the last line in webp_functions[] */
};
/* }}} */

/* {{{ webp_module_entry
 */
zend_module_entry webp_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"webp",
	webp_functions,
	PHP_MINIT(webp),
	PHP_MSHUTDOWN(webp),
	PHP_RINIT(webp),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(webp),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(webp),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_WEBP
ZEND_GET_MODULE(webp)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("webp.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_webp_globals, webp_globals)
    STD_PHP_INI_ENTRY("webp.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_webp_globals, webp_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_webp_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_webp_init_globals(zend_webp_globals *webp_globals)
{
	webp_globals->global_value = 0;
	webp_globals->global_string = NULL;
}
*/
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(webp)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(webp)
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
PHP_RINIT_FUNCTION(webp)
{
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(webp)
{
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(webp)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "webp support", "enabled");
	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


PHP_FUNCTION(image2webp)
{
	char *input_file, *output_file;
	int input_file_len, output_file_len;
	int input_file_fd = -1, output_file_fd = -1;
	struct stat sbuf;

	char *blob = NULL;
	int blob_size = 0;
	int result = 1;

    out_buf_t out_buf;
    out_buf.start = emalloc(2*1024*1024); // 2m
    out_buf.len = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss",
		&input_file, &input_file_len, &output_file, &output_file_len) == FAILURE)
	{
		RETURN_FALSE;
	}

	input_file_fd = open(input_file, O_RDONLY);
	if (input_file_fd == -1) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Can not open input image");
		result = 0;
		goto outh;
	}

	output_file_fd = open(output_file, O_WRONLY|O_CREAT, 0666);
	if (!output_file_fd) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Can not open output image");
		result = 0;
		goto outh;
	}

	// read input image file size
	if (fstat(input_file_fd, &sbuf) == -1) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Can not read input image stat");
		result = 0;
		goto outh;
	}

	blob_size = sbuf.st_size;
	blob = emalloc(blob_size);

	if (read(input_file_fd, blob, blob_size) != blob_size) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Can not read from input image");
		result = 0;
		goto outh;
	}

	EncodeImage2Webp(blob, blob_size, &out_buf);

	if (write(output_file_fd, out_buf.start, out_buf.len) != out_buf.len) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Can not write to output image");
		result = 0;
		goto outh;
	}

outh:
	if (input_file_fd != -1)
		close(input_file_fd);
	if (output_file_fd != -1)
		close(output_file_fd);

	if (result) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}


