--TEST--
switchTo capability: returns the body's value and re-raises its exception
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
require __DIR__ . '/switch_harness.inc';

$s = register_switch_harness();

var_dump(($s->switch)($s->coroutine(fn () => 42)));
var_dump(($s->switch)($s->coroutine(fn () => "hi")));

try {
    ($s->switch)($s->coroutine(function () {
        throw new RuntimeException("boom");
    }));
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

echo "survived\n";
?>
--EXPECT--
int(42)
string(2) "hi"
caught: boom
survived
