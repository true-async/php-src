--TEST--
Async\SchedulerHook::register activates and getModule() reports the driver
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
var_dump(Async\SchedulerHook::getModule());

Async\SchedulerHook::register('my-driver', fn () => new class implements \Async\Scheduler {
    public function onLaunch(): object { return $this->main ??= new stdClass(); }
    public ?object $main = null;
    public function onShutdown(): void {}
    public function onFiber(\Fiber $fiber): ?object { return null; }
    public function onDefer(callable $task): void {}
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
});

var_dump(Async\SchedulerHook::getModule());
?>
--EXPECT--
NULL
string(9) "my-driver"
