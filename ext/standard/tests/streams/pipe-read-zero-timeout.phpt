--TEST--
A pipe read with a zero stream_set_timeout() only looks for data, as a socket read does
--SKIPIF--
<?php
if (!function_exists('proc_open')) die('skip no proc_open');
?>
--ENV--
PHP_IO_OVERLAPPED_PIPES=1
--FILE--
<?php
$process = proc_open([PHP_BINARY, '-r', 'fgets(STDIN);'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);
stream_set_timeout($pipes[1], 0);

var_dump(fread($pipes[1], 10));
$meta = stream_get_meta_data($pipes[1]);
var_dump($meta['timed_out'], $meta['eof']);

fclose($pipes[0]);
fclose($pipes[1]);
proc_close($process);
?>
--EXPECT--
string(0) ""
bool(false)
bool(false)
