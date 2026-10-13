--TEST--
stream_set_timeout() on the write end of a pipe stays unsupported
--SKIPIF--
<?php
if (!function_exists('proc_open')) die('skip no proc_open');
?>
--FILE--
<?php
$process = proc_open([PHP_BINARY, '-r', 'fgets(STDIN);'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);
var_dump(stream_set_timeout($pipes[0], 1));

fclose($pipes[0]);
fclose($pipes[1]);
proc_close($process);
?>
--EXPECT--
bool(false)
