--TEST--
After-main handover drains coroutines that became runnable at script end
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

$fiber = new Fiber(function (): void {
    echo "started\n";
    Fiber::suspend();
    echo "resumed by the after-main drain\n";
});

$fiber->start();

// Nobody resumes the parked fiber during the script: hand its coroutine back
// to the queue, so the after-main handover picks it up.
$scheduler->onEnqueue($scheduler->lastAdopted);

echo "end of script\n";
?>
--EXPECT--
started
end of script
resumed by the after-main drain
