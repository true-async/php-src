--TEST--
stream_socket_recvfrom() that ends at the stream's timeout leaves the peer address unset
--FILE--
<?php
$socket = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
stream_set_timeout($socket, 0, 100000);

$peer = 'unset';
var_dump(stream_socket_recvfrom($socket, 10, 0, $peer));
var_dump($peer);
?>
--EXPECT--
bool(false)
NULL
