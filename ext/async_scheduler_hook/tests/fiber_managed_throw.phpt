--TEST--
Fiber::throw() on a managed fiber delivers the exception at the suspension point
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
require __DIR__ . '/mini_scheduler.inc';

register_mini_scheduler();

$fiber = new Fiber(function (): string {
    try {
        Fiber::suspend('waiting');
    } catch (RuntimeException $e) {
        return 'caught: ' . $e->getMessage();
    }
    return 'not reached';
});

var_dump($fiber->start());
$fiber->throw(new RuntimeException('boom'));
var_dump($fiber->getReturn());
?>
--EXPECT--
string(7) "waiting"
string(12) "caught: boom"
