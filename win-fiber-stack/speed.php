<?php
// Usage: php speed.php fiber-new|spawn-suspended <count>
// fiber-new: creates, starts and drops <count> fibers one after another: one stack allocation and free each.
// spawn-suspended: spawns <count> coroutines that stay suspended: <count> live stacks at once.
// Prints the time per operation in microseconds.
use function Async\spawn;
use function Async\suspend;
use function Async\delay;

$mode = $argv[1] ?? 'fiber-new';
$count = (int) ($argv[2] ?? 100000);
$keep = [];
$started = hrtime(true);

if ($mode === 'fiber-new') {
    for ($i = 0; $i < $count; $i++) {
        $fiber = new Fiber(static function () {
            return 1;
        });
        $fiber->start();
    }
} else {
    for ($i = 0; $i < $count; $i++) {
        $keep[] = spawn(static function () {
            delay(3600000);
        });
    }

    suspend();
}

printf("%s %d: %.3f us/op\n", $mode, $count, (hrtime(true) - $started) / $count / 1000);

foreach ($keep as $coroutine) {
    $coroutine->cancel();
}
