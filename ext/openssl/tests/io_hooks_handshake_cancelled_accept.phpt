--TEST--
IO hooks: a TLS server handshake whose wait throws ends at once and reports only the exception
--EXTENSIONS--
openssl
--FILE--
<?php
$certFile = __DIR__ . DIRECTORY_SEPARATOR . 'io_hooks_handshake_cancelled_accept.pem.tmp';
include 'CertificateGenerator.inc';
(new CertificateGenerator())->saveNewCertAsFileWithKey('io_hooks_handshake_cancelled_accept', $certFile);

$context = stream_context_create(['ssl' => ['local_cert' => $certFile]]);
$server = stream_socket_server('ssl://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);

/* The client connects and never sends a ClientHello */
$client = stream_socket_client('tcp://' . stream_socket_get_name($server, false));

Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        return match (true) {
            $op instanceof Io\Operation\Accept, $op instanceof Io\Operation\Send
                => $op->complete(Io\CompletionStatus::Unsupported),
            default => throw new RuntimeException("cancelled " . $op::class),
        };
    }
});

try {
    var_dump(stream_socket_accept($server, 2));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
?>
--CLEAN--
<?php
@unlink(__DIR__ . DIRECTORY_SEPARATOR . 'io_hooks_handshake_cancelled_accept.pem.tmp');
?>
--EXPECT--
cancelled Io\Operation\Recv
