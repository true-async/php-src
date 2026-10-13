--TEST--
IO hooks: stream_socket_sendto() whose name lookup throws reports only the exception
--FILE--
<?php
$socket = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(stream_socket_sendto($socket, 'x', 0, 'localhost:9'));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
cancelled Io\Operation\GetAddrInfo
