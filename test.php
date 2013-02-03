<?php

if (!extension_loaded("preprocessor")) {
	dl("preprocessor.so");
	include __FILE__;
}

eval('print "zzz...\n\n";');
