--TEST--
Async\get_context(): per-coroutine isolation and the explicit-object form
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
require __DIR__ . '/mini_scheduler.inc';

$scheduler = register_mini_scheduler();

// The main coroutine (minted by onLaunch) carries the store of the flow the
// script runs in; the coroutines below get their own.
Async\get_context()->set('who', 'main');

$make = static fn (string $name) => new Fiber(function () use ($name) {
    Async\get_context()->set('who', $name);
    Fiber::suspend();
    var_dump(Async\get_context()->find('who'));
});

$fiberA = $make('A');
$fiberB = $make('B');
$fiberA->start();
$fiberB->start();

// Reaching the parked coroutines' contexts from outside, through the
// scheduler's opaque coroutine objects (the explicit-object form).
var_dump(Async\get_context($scheduler->adopted[0])->find('who'));
var_dump(Async\get_context($scheduler->adopted[1])->find('who'));
var_dump(Async\get_context()->find('who'));

// Each coroutine still sees its own value when it resumes.
$fiberA->resume();
$fiberB->resume();
var_dump(Async\get_context()->find('who'));

?>
--EXPECT--
string(1) "A"
string(1) "B"
string(4) "main"
string(1) "A"
string(1) "B"
string(4) "main"
