--TEST--
IO hooks: stream_socket_accept() whose operation completes Cancelled with no exception still warns
--FILE--
<?php
$server = stream_socket_server('tcp://127.0.0.1:0');
Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        return $op->complete(Io\CompletionStatus::Cancelled);
    }
});

var_dump(stream_socket_accept($server, 5));
?>
--EXPECTF--
Warning: stream_socket_accept(): Accept failed: %s in %s on line %d
bool(false)
