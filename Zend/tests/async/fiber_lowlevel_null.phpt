--TEST--
intercept_fiber returning null keeps the fiber on the low-level path
--FILE--
<?php
Async\SchedulerHook::register('test', [
    Async\SchedulerHook::INTERCEPT_FIBER => function (Fiber $fiber): ?object {
        echo "intercept: null\n";
        return null;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout): bool {
        // Never called for the low-level fiber itself; the engine invokes it
        // for the after-main handover (script end, then after destructors).
        echo "suspend(fromMain: ", var_export($fromMain, true), ")\n";
        return true;
    },
]);

// A low-level fiber behaves exactly as classic Fiber even with a scheduler.
$fiber = new Fiber(function (int $x): int {
    $y = Fiber::suspend($x + 1);
    return $y * 10;
});

var_dump($fiber->start(5));
var_dump($fiber->resume(4));
var_dump($fiber->getReturn());
?>
--EXPECT--
intercept: null
int(6)
NULL
int(40)
suspend(fromMain: true)
suspend(fromMain: true)
