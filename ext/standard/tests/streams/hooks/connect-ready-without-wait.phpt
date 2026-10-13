--TEST--
IO hooks: a Ready a provider answers without a wait does not end a connect still under way
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') die('skip Linux drops a SYN to a full accept queue');
?>
--FILE--
<?php
final class ReadyOnly implements Io\Hooks\Hooks
{
    public array $ops = [];
    public function getCapabilities(): array { return []; }
    public function run(Io\Operation $op): Io\Completion
    {
        $this->ops[] = $op::class;
        return $op instanceof Io\Operation\Connect
            ? $op->completeReady($op->getEvents())
            : $op->complete(Io\CompletionStatus::Unsupported);
    }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
}

/* With a backlog of 0 the queue holds one connection: the second connect stays under way */
$context = stream_context_create(['socket' => ['backlog' => 0]]);
$server = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr,
    STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);
$address = stream_socket_get_name($server, false);
$first = stream_socket_client("tcp://$address");

$provider = new ReadyOnly();
Io\Hooks\set_hooks($provider);
$second = @stream_socket_client("tcp://$address", $errno, $errstr, 0.3);
Io\Hooks\set_hooks(null);
var_dump($second, $errstr, $provider->ops);
?>
--EXPECT--
bool(false)
string(20) "Connection timed out"
array(2) {
  [0]=>
  string(20) "Io\Operation\Connect"
  [1]=>
  string(17) "Io\Operation\Poll"
}
