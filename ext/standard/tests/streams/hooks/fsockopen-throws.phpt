--TEST--
IO hooks: fsockopen() whose connect throws reports only the exception
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
[$host, $port] = explode(':', stream_socket_get_name($server, false));
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(fsockopen($host, (int) $port, $errno, $errstr, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECTREGEX--
cancelled Io\\Operation\\(Connect|Poll)
