--TEST--
onFiber returning null keeps the fiber on the low-level path
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
Async\SchedulerHook::register('test', fn () => new class implements \Async\Scheduler {
    public function onLaunch(): object { return $this->main ??= new stdClass(); }
    public ?object $main = null;
    public function onShutdown(): void {}
    public function onDefer(callable $task): void {}
    public function onFiber(Fiber $fiber): ?object {
        echo "intercept: null\n";
        return null;
    }
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        // Never called for the low-level fiber itself; the engine invokes it
        // for the after-main handover (script end, then after destructors),
        // and each handover replaces the finished main with a fresh one.
        echo "suspend(fromMain: ", var_export($fromMain, true), ")\n";
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
});

// A low-level fiber behaves exactly as classic Fiber even with a scheduler.
$fiber = new Fiber(function (int $x): int {
    $y = Fiber::suspend($x + 1);
    return $y * 10;
});

var_dump($fiber->start(5));
var_dump($fiber->resume(4));
var_dump($fiber->getReturn());
?>
--EXPECT--
intercept: null
int(6)
NULL
int(40)
suspend(fromMain: true)
suspend(fromMain: true)
