--TEST--
Async\SchedulerHook::register can be called only once
--FILE--
<?php
$noop = function (bool $fromMain, bool $isBailout): bool { return true; };

// First registration succeeds.
var_dump(Async\SchedulerHook::register('a', [
    Async\SchedulerHook::SUSPEND => $noop,
]));

// A second registration throws: a scheduler is registered once per process.
try {
    Async\SchedulerHook::register('b', [
        Async\SchedulerHook::SUSPEND => $noop,
    ]);
} catch (\Error $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
bool(true)
A scheduler is already registered
