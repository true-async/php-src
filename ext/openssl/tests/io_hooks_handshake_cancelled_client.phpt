--TEST--
IO hooks: a TLS client handshake whose wait throws ends at once and reports only the exception
--EXTENSIONS--
openssl
--FILE--
<?php
/* The server accepts nothing and never answers the ClientHello */
$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);

Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        return match (true) {
            $op instanceof Io\Operation\Connect, $op instanceof Io\Operation\Poll => $op->completeReady($op->getEvents()),
            $op instanceof Io\Operation\Send => $op->complete(Io\CompletionStatus::Unsupported),
            default => throw new RuntimeException("cancelled " . $op::class),
        };
    }
});

try {
    var_dump(stream_socket_client("tls://$address", $errno, $errstr, 2));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
cancelled Io\Operation\Recv
