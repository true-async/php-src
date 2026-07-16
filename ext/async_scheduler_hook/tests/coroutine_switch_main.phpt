--TEST--
switchTo capability: the main coroutine is switchable like any other
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

// The main flow is addressable through its coroutine object: no separate
// capture step, onLaunch already defined it.
$c = $s->coroutine(function () use ($s): void {
    echo "  in coroutine\n";
    $back = ($s->switch)($s->main, 'back to main');   // symmetric jump into main
    echo "  finishing ($back)\n";
});

var_dump(($s->switch)($c));
echo "main resumed\n";
($s->switch)($c, 'bye');    // let the body run to completion
echo "done\n";
?>
--EXPECT--
  in coroutine
string(12) "back to main"
main resumed
  finishing (bye)
done
