--TEST--
IO hooks: a connect whose operation throws tries no further address
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
$port = (int) substr(strrchr(stream_socket_get_name($server, false), ':'), 1);

Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        if ($op instanceof Io\Operation\GetAddrInfo) {
            return $op->completeWithAddresses(['127.0.0.1', '127.0.0.1']);
        }
        throw new RuntimeException("cancelled " . $op::class);
    }
});

try {
    var_dump(stream_socket_client("tcp://two.addresses:$port", $errno, $errstr, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
Io\Hooks\set_hooks(null);
/* Each attempt's connect() reaches the listener even when the provider throws, so a second attempt
 * would show as a second connection; the first may close before it is accepted */
$accepted = 0;
while (@stream_socket_accept($server, 0.2)) {
    $accepted++;
}
var_dump($accepted <= 1);
?>
--EXPECTREGEX--
cancelled Io\\Operation\\(Connect|Poll)
bool\(true\)
