--TEST--
A persistent tcp:// client stream opened after the reactor started survives its resource being released
--EXTENSIONS--
openssl
true_async
--FILE--
<?php
// With openssl loaded, tcp:// goes through the openssl socket factory. A
// persistent stream outlives the request, so it must stay off the reactor:
// the reactor's handles are request memory.
$server = stream_socket_server("tcp://127.0.0.1:0", $errno, $errstr);
$addr = stream_socket_get_name($server, false);

function client(string $addr) {
    $fp = stream_socket_client("tcp://$addr", $errno, $errstr, 5,
        STREAM_CLIENT_CONNECT | STREAM_CLIENT_PERSISTENT);
    fwrite($fp, "ping\n");
    return $fp;
}

file(__FILE__); // starts the reactor
$fp = client($addr);
$conn = stream_socket_accept($server, 5);
echo "server got: ", fgets($conn);
fwrite($conn, "pong\n");
echo "client got: ", fgets($fp);
$fp = null; // the request resource goes, the persistent stream stays
echo "done\n";
?>
--EXPECT--
server got: ping
client got: pong
done
