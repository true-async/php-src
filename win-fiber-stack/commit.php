<?php
// Usage: php commit.php fiber|coroutine <count>
// Leaves <count> fibers or coroutines suspended, each on its own stack, prints "ready" and waits to be killed.
// measure-commit.ps1 reads the process's commit charge (private bytes) at that point.
use function Async\spawn;
use function Async\suspend;
use function Async\delay;

$mode = $argv[1] ?? 'fiber';
$count = (int) ($argv[2] ?? 1000);
$keep = [];

if ($mode === 'fiber') {
    for ($i = 0; $i < $count; $i++) {
        $fiber = new Fiber(static function () {
            Fiber::suspend();
        });
        $fiber->start();
        $keep[] = $fiber;
    }
} else {
    for ($i = 0; $i < $count; $i++) {
        $keep[] = spawn(static function () {
            delay(3600000);
        });
    }

    // Each coroutine gets its stack on its first run and parks in delay().
    suspend();
}

echo "ready\n";
fflush(STDOUT);

if ($mode === 'fiber') {
    sleep(3600);
} else {
    delay(3600000);
}
