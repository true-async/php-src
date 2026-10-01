--TEST--
Io\Poll\ProcessHandle: the status of a child it reaped goes to the next wait for it, with rusage and by group, and not to a forked child
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsProcessHandles()) die("skip no process handle source on this platform");
?>
--FILE--
<?php
use Io\Poll\{Context, Event, ProcessHandle};

function reap_via_handle(int $code): int {
    $pid = pcntl_fork();
    if ($pid == 0) {
        exit($code);
    }
    $h = new ProcessHandle($pid);
    $c = new Context;
    $c->add($h, [Event::Process]);
    $c->wait(Time\Duration::fromSeconds(5));
    return $pid;
}

echo "-- pcntl_waitpid with rusage\n";
$a = reap_via_handle(3);
var_dump(pcntl_waitpid($a, $st, 0, $ru) === $a, pcntl_wexitstatus($st), is_array($ru) && array_sum($ru) === 0);
var_dump(pcntl_waitpid($a, $st));

echo "-- pcntl_wait with rusage\n";
$a = reap_via_handle(4);
var_dump(pcntl_wait($st, 0, $ru) === $a, pcntl_wexitstatus($st));

echo "-- by process group\n";
$a = reap_via_handle(5);
var_dump(pcntl_waitpid(0, $st) === $a, pcntl_wexitstatus($st));
$a = reap_via_handle(6);
var_dump(pcntl_waitpid(-posix_getpgrp(), $st) === $a, pcntl_wexitstatus($st));
$a = reap_via_handle(7);
var_dump(pcntl_waitpid(-999999, $st, WNOHANG));
var_dump(pcntl_waitpid($a, $st) === $a, pcntl_wexitstatus($st));

echo "-- not inherited by a forked child\n";
$a = reap_via_handle(8);
$b = pcntl_fork();
if ($b == 0) {
    var_dump(pcntl_wait($st, WNOHANG));
    exit(0);
}
pcntl_waitpid($b, $st);
var_dump(pcntl_waitpid($a, $st) === $a, pcntl_wexitstatus($st));
?>
--EXPECT--
-- pcntl_waitpid with rusage
bool(true)
int(3)
bool(true)
int(-1)
-- pcntl_wait with rusage
bool(true)
int(4)
-- by process group
bool(true)
int(5)
bool(true)
int(6)
int(-1)
bool(true)
int(7)
-- not inherited by a forked child
int(-1)
bool(true)
int(8)
