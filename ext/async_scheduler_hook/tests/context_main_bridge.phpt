--TEST--
Async\get_context(): the main coroutine carries the script's store from the launch on
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

// The scheduler launched inside register(): the script continues in the main
// coroutine, and its store is reachable from here on.
Async\get_context()->set('who', 'main');

$fiber = new Fiber(function () {
    Async\get_context()->set('who', 'fiber');
    var_dump(Async\get_context()->find('who'));
});

$fiber->start();

var_dump(Async\get_context()->find('who'));

// The same store through the scheduler's own main coroutine object.
var_dump(Async\get_context($scheduler->main)->find('who'));
var_dump(Async\get_context($scheduler->main) === Async\get_context());

?>
--EXPECT--
string(5) "fiber"
string(4) "main"
string(4) "main"
bool(true)
