--TEST--
stream_set_timeout() bounds a read of a proc_open() pipe
--SKIPIF--
<?php
if (!function_exists('proc_open')) die('skip no proc_open');
?>
--ENV--
PHP_IO_OVERLAPPED_PIPES=1
--FILE--
<?php
/* The child writes nothing until its stdin closes */
$process = proc_open([PHP_BINARY, '-r', 'fgets(STDIN);'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

var_dump(stream_set_timeout($pipes[1], 0, 100000));
var_dump(fread($pipes[1], 10));
$meta = stream_get_meta_data($pipes[1]);
var_dump($meta['timed_out'], $meta['eof']);

fclose($pipes[0]);
stream_set_timeout($pipes[1], 30);
var_dump(fread($pipes[1], 10));
var_dump(stream_get_meta_data($pipes[1])['timed_out']);
fclose($pipes[1]);
proc_close($process);
?>
--EXPECT--
bool(true)
bool(false)
bool(true)
bool(false)
string(0) ""
bool(false)
