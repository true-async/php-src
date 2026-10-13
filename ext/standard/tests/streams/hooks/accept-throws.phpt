--TEST--
IO hooks: stream_socket_accept() whose operation throws reports only the exception
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(stream_socket_accept($server, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
cancelled Io\Operation\Accept
