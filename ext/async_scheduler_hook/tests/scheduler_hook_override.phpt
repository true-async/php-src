--TEST--
Async\SchedulerHook::register can be called only once
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
$make = fn () => new class implements \Async\Scheduler {
    public function onLaunch(): object { return $this->main ??= new stdClass(); }
    public ?object $main = null;
    public function onShutdown(): void {}
    public function onFiber(\Fiber $fiber): ?object { return null; }
    public function onDefer(callable $task): void {}
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
};

// First registration succeeds.
Async\SchedulerHook::register('a', $make);
echo "registered\n";

// A second registration throws: a scheduler is registered once per process.
try {
    Async\SchedulerHook::register('b', $make);
} catch (\Error $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
registered
A scheduler is already registered
