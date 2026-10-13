--TEST--
stream_socket_recvfrom() on a blocking UDP socket ends at the stream's timeout
--FILE--
<?php
$socket = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
stream_set_timeout($socket, 0, 100000);

var_dump(stream_socket_recvfrom($socket, 10));
var_dump(stream_get_meta_data($socket)['timed_out']);
?>
--EXPECT--
bool(false)
bool(true)
