PHP_ARG_ENABLE(preprocessor, whether to enable preprocessor support,
Make sure that the comment is aligned:
[  --enable-preprocessor           Enable preprocessor support])

if test "$PHP_PREPROCESSOR" != "no"; then
	PHP_NEW_EXTENSION(preprocessor, preprocessor.c, $ext_shared)
	PHP_SUBST(PREPROCESSOR_SHARED_LIBADD)
	PHP_ADD_MAKEFILE_FRAGMENT
fi
