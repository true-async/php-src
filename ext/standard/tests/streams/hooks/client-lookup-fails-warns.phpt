--TEST--
IO hooks: stream_socket_client() whose name lookup finds no address with no exception still warns
--FILE--
<?php
Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        return $op->completeWithAddresses([]);
    }
});

var_dump(stream_socket_client('tcp://nowhere.provider:80', $errno, $errstr, 5));
?>
--EXPECTF--
Warning: stream_socket_client(): php_network_getaddresses: getaddrinfo for nowhere.provider failed: %s in %s on line %d

Warning: stream_socket_client(): Unable to connect to tcp://nowhere.provider:80 (php_network_getaddresses: getaddrinfo for nowhere.provider failed: %s) in %s on line %d
bool(false)
