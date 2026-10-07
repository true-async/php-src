--TEST--
Concurrent HTTP wrappers release the previous last-response headers on completion
--EXTENSIONS--
true_async
--INI--
allow_url_fopen=1
--FILE--
<?php
use function Async\{spawn, await};

$listener = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($listener, false);
$server = spawn(function () use ($listener) {
    $sockets = [];
    // Wait until all wrappers are inside their I/O before completing any response.
    for ($i = 0; $i < 3; ++$i) {
        $socket = stream_socket_accept($listener);
        do {
            $line = fgets($socket);
        } while ($line !== false && $line !== "\r\n");
        $sockets[] = $socket;
    }
    foreach ($sockets as $socket) {
        fwrite($socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
        fclose($socket);
    }
    fclose($listener);
});
$clients = [];
for ($i = 0; $i < 3; ++$i) {
    $clients[] = spawn(static fn() => file_get_contents("http://$address/"));
}
foreach ($clients as $client) {
    var_dump(await($client));
}
await($server);
var_dump(http_get_last_response_headers());
http_clear_last_response_headers();
var_dump(http_get_last_response_headers());
?>
--EXPECT--
string(2) "ok"
string(2) "ok"
string(2) "ok"
array(3) {
  [0]=>
  string(15) "HTTP/1.1 200 OK"
  [1]=>
  string(17) "Content-Length: 2"
  [2]=>
  string(17) "Connection: close"
}
NULL
