--TEST--
A fiber adopted by the scheduler runs through the coroutine path
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

$fiber = new Fiber(function (int $x): int {
    $y = Fiber::suspend($x + 1);
    return $y * 10;
});

var_dump($fiber->start(5));
var_dump($fiber->isSuspended());
var_dump($fiber->resume(4));
var_dump($fiber->isTerminated());
var_dump($fiber->getReturn());
echo implode(',', $scheduler->log), "\n";
?>
--EXPECT--
int(6)
bool(true)
NULL
bool(true)
int(40)
intercept,enqueue,suspend,enqueue,suspend,enqueue,suspend,enqueue
