--TEST--
IO hooks: stream_socket_recvfrom() hands the provider the stream's timeout and reports its Timeout
--FILE--
<?php
$socket = stream_socket_server('udp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND);
stream_set_timeout($socket, 5);

Io\Hooks\set_hooks(new class implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        $timeout = $op->getTimeout();
        echo $op::class, ", timeout ", $timeout === null ? 'none' : ($timeout->seconds <= 5 ? 'up to 5 s' : 'over 5 s'), "\n";
        return $op->complete(Io\CompletionStatus::Timeout);
    }
});

var_dump(stream_socket_recvfrom($socket, 10));
var_dump(stream_get_meta_data($socket)['timed_out']);
?>
--EXPECT--
Io\Operation\Poll, timeout up to 5 s
bool(false)
bool(true)
