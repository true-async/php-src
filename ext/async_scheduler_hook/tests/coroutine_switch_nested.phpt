--TEST--
switchTo capability: a coroutine switches into another mid-run; the inner returns to the outer (symmetric)
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

$b = $s->coroutine(function (): void {
    echo "  B: runs to completion\n";
    // returns -> control goes back to B's switcher (A)
});

$a = $s->coroutine(function () use ($s, $b): void {
    echo "  A: 1\n";
    ($s->switch)($b);          // A -> B mid-run; B completes -> back to A
    echo "  A: 2 (after B)\n";
});

echo "main -> A\n";
($s->switch)($a);              // main -> A -> B(done) -> A(done) -> main
echo "main: done\n";
?>
--EXPECT--
main -> A
  A: 1
  B: runs to completion
  A: 2 (after B)
main: done
