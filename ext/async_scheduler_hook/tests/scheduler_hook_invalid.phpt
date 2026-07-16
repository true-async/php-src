--TEST--
Async\SchedulerHook::register validates the factory and what it returns
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
// The factory argument must be callable.
try {
    Async\SchedulerHook::register('test', new stdClass());
} catch (\TypeError $e) {
    echo $e->getMessage(), "\n";
}

// The factory must return an Async\Scheduler instance.
try {
    Async\SchedulerHook::register('test', fn () => new stdClass());
} catch (\TypeError $e) {
    echo $e->getMessage(), "\n";
}

// A refused registration leaves no active scheduler.
var_dump(Async\SchedulerHook::getModule());
?>
--EXPECTF--
Async\SchedulerHook::register(): Argument #2 ($factory) must be a valid callback, %s
Async\SchedulerHook::register(): Argument #2 ($factory) must return an instance of Async\Scheduler
NULL
