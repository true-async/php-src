--TEST--
IO hooks: stream_socket_client() whose name lookup throws reports only the exception
--FILE--
<?php
require __DIR__ . '/provider-throws.inc';

try {
    var_dump(stream_socket_client('tcp://localhost:80', $errno, $errstr, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
cancelled Io\Operation\GetAddrInfo
