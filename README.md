Preprocessor
============

A liitle C-like preprocessor for php

Installation on Unix:
```shell
git clone git://github.com/bwoebi/preprocessor.git
cd preprocessor
phpize
./configure --with-php-config=`which php-config`
make install
```
Add to the php.ini the following line:
	extension=preprocessor.so ; on windows: preprocessor.dll
