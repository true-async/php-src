--TEST--
A pipe opened for writing is closed at shutdown even when the script leaves it open
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') die('skip POSIX only');
if (!function_exists('popen')) die('skip popen() is disabled');
?>
--FILE--
<?php

/* The child reads until EOF, so it only exits once every copy of the write end
 * is closed; pclose() at request shutdown waits for it. */
$pipe = popen('cat > /dev/null', 'w');
fwrite($pipe, "data\n");

echo "wrote\n";

?>
--EXPECT--
wrote
