--TEST--
IO hooks: stream_socket_client() whose connect throws reports only the exception
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(stream_socket_client("tcp://$address", $errno, $errstr, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECTREGEX--
cancelled Io\\Operation\\(Connect|Poll)
