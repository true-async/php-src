--TEST--
switchTo capability: the body runs on the coroutine's own stack and returns to the switcher
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

$a = $s->coroutine(function (): void {
    echo "  A\n";
});

$b = $s->coroutine(function (): void {
    $sum = 0;
    for ($i = 0; $i < 1000; $i++) {
        $sum += $i;   // real VM work on the coroutine's own stack
    }
    echo "  B sum=$sum\n";
});

echo "before\n";
($s->switch)($a);
($s->switch)($b);
echo "after\n";
?>
--EXPECT--
before
  A
  B sum=499500
after
