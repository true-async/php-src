--TEST--
switchTo capability: $error is delivered at the suspension point; boundary cases
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
require __DIR__ . '/switch_harness.inc';

$s = register_switch_harness();

// 1. An error is thrown from the switchTo() the target is suspended in.
$worker = $s->coroutine(function () use ($s): void {
    echo "  worker started\n";
    try {
        ($s->switch)($s->main, "parked");
    } catch (\RuntimeException $e) {
        echo "  caught at suspension point: {$e->getMessage()}\n";
    }
});

var_dump(($s->switch)($worker));
($s->switch)($worker, null, new \RuntimeException("cancelled"));

// 2. Passing both a non-null value and an error is a ValueError.
$other = $s->coroutine(function (): void {});
try {
    ($s->switch)($other, "value", new \RuntimeException("boom"));
} catch (\ValueError $e) {
    echo "ValueError\n";
}

// 3. A first entry with an error finishes the coroutine without starting
// the body; the error surfaces at the switch site.
$never = $s->coroutine(function (): void {
    echo "  never printed\n";
});
try {
    ($s->switch)($never, null, new \LogicException("cancel before start"));
} catch (\LogicException $e) {
    echo "first entry: {$e->getMessage()}\n";
}

// 4. Switching into a finished coroutine throws an Error.
try {
    ($s->switch)($never);
} catch (\Error $e) {
    echo $e->getMessage(), "\n";
}

// 5. A coroutine with no body cannot run.
try {
    ($s->switch)(new stdClass());
} catch (\Error $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
  worker started
string(6) "parked"
  caught at suspension point: cancelled
ValueError
first entry: cancel before start
Cannot switch into a finished coroutine
The object is not a coroutine the engine knows
