--TEST--
IO hooks: a read of an overlapped pipe with stream_set_timeout() waits for data in a Poll op with the timeout
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') die('skip only Windows makes an overlapped pipe');
if (!function_exists('proc_open')) die('skip no proc_open');
?>
--FILE--
<?php
class TimeoutHooks implements Io\Hooks\Hooks {
    public function getCapabilities(): array { return []; }
    public function add(Io\Registration $registration): void {}
    public function remove(Io\Registration $registration): void {}
    public function run(Io\Operation $op): Io\Completion {
        if (!$op instanceof Io\Operation\Poll) {
            return $op->complete(Io\CompletionStatus::Unsupported);
        }
        $timeout = $op->getTimeout();
        echo $op::class, ", timeout ", $timeout === null ? 'none' : ($timeout->seconds <= 5 ? 'up to 5 s' : 'over 5 s'), "\n";
        return $op->complete(Io\CompletionStatus::Timeout);
    }
}

/* Installed before proc_open(), so the pipe is overlapped */
Io\Hooks\set_hooks(new TimeoutHooks());
$process = proc_open([PHP_BINARY, '-r', 'fgets(STDIN);'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);
var_dump(stream_set_timeout($pipes[1], 5));

var_dump(fread($pipes[1], 10));
var_dump(stream_get_meta_data($pipes[1])['timed_out']);
Io\Hooks\set_hooks(null);

fclose($pipes[0]);
fclose($pipes[1]);
proc_close($process);
?>
--EXPECT--
bool(true)
Io\Operation\Poll, timeout up to 5 s
bool(false)
bool(true)
