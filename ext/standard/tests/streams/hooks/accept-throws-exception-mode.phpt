--TEST--
IO hooks: stream_socket_accept() whose operation throws, on a listener in the exception mode, throws that exception
--FILE--
<?php
$context = stream_context_create(['stream' => ['error_mode' => StreamErrorMode::Exception]]);
$server = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(stream_socket_accept($server, 5));
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
RuntimeException: cancelled Io\Operation\Accept
