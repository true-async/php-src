--TEST--
An uncaught exception in a managed fiber surfaces at the start()/resume() caller
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

$fiber = new Fiber(function (): void {
    throw new RuntimeException('escaped');
});

try {
    $fiber->start();
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

var_dump($fiber->isTerminated());
?>
--EXPECT--
caught: escaped
bool(true)
