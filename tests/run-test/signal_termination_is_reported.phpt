--TEST--
A test killed by a signal reports Termsig even when its pipes close first
--SKIPIF--
<?php
if (!function_exists("posix_kill")) die("skip no posix_kill");
if (PHP_OS_FAMILY === 'Windows') die("skip POSIX signals only");
?>
--FILE--
<?php
echo "before the signal\n";
posix_kill(getmypid(), SIGKILL);
?>
--EXPECTF--
before the signal
%A
Termsig=%d
