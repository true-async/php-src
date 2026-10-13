--TEST--
IO hooks: a connect whose operation throws tries no further address
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
$port = (int) substr(strrchr(stream_socket_get_name($server, false), ':'), 1);

$provider = new class implements Io\Hooks\Hooks {
    public int $connects = 0;
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        if ($op instanceof Io\Operation\GetAddrInfo) {
            return $op->completeWithAddresses(['127.0.0.1', '127.0.0.2']);
        }
        $this->connects++;
        throw new RuntimeException("cancelled " . $op::class);
    }
};
Io\Hooks\set_hooks($provider);

try {
    var_dump(stream_socket_client("tcp://two.addresses:$port", $errno, $errstr, 5));
} catch (RuntimeException $e) {
    echo $e->getMessage(), "\n";
}
var_dump($provider->connects);
?>
--EXPECT--
cancelled Io\Operation\Connect
int(1)
