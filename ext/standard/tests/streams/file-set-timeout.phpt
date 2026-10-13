--TEST--
stream_set_timeout() on a regular file stays unsupported
--FILE--
<?php
$file = fopen(__FILE__, 'r');
var_dump(stream_set_timeout($file, 1));
?>
--EXPECT--
bool(false)
