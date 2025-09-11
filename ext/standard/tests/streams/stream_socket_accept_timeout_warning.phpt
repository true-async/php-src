--TEST--
Test stream_socket_accept() timeout warning
--FILE--
<?php
$server = stream_socket_server("tcp://127.0.0.1:0");

// Try to accept with a short timeout (1 second)
// Since no client connects, this should timeout
$client = stream_socket_accept($server, 1);

var_dump($client);

fclose($server);
?>
--EXPECTF--
Warning: stream_socket_accept(): Accept failed: %a
bool(false)