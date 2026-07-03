--TEST--
Async\SchedulerHook::register rejects a non-callable hook
--FILE--
<?php
try {
    Async\SchedulerHook::register('test', [
        Async\SchedulerHook::SUSPEND => 'this_function_does_not_exist',
    ]);
} catch (\TypeError $e) {
    echo $e->getMessage(), "\n";
}

// A refused registration leaves no active scheduler.
var_dump(Async\SchedulerHook::getModule());
?>
--EXPECTF--
Async scheduler hook "suspend" must be a valid callable: %s
NULL
