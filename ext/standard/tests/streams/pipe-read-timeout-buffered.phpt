--TEST--
A pipe read with stream_set_timeout() returns the bytes already buffered without waiting
--SKIPIF--
<?php
if (!function_exists('proc_open')) die('skip no proc_open');
?>
--ENV--
PHP_IO_OVERLAPPED_PIPES=1
--FILE--
<?php
/* The child writes a line and a half, then nothing until its stdin closes */
$process = proc_open([PHP_BINARY, '-r', 'echo "line1\nab"; fflush(STDOUT); fgets(STDIN);'],
    [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);
stream_set_timeout($pipes[1], 30);

var_dump(fgets($pipes[1]));
var_dump(fread($pipes[1], 100));
var_dump(stream_get_meta_data($pipes[1])['timed_out']);

fclose($pipes[0]);
fclose($pipes[1]);
proc_close($process);
?>
--EXPECT--
string(6) "line1
"
string(2) "ab"
bool(false)
