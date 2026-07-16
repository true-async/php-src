--TEST--
Async\Scheduler: interface shape
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
$r = new ReflectionClass(Async\Scheduler::class);
var_dump($r->isInterface());

$methods = array_map(fn ($m) => $m->name, $r->getMethods());
sort($methods);
echo implode(',', $methods), "\n";
?>
--EXPECT--
bool(true)
onDefer,onEnqueue,onFiber,onLaunch,onShutdown,onSuspend
