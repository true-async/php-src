--TEST--
Microtasks: defer() forwards to the scheduler's onDefer() hook; the queue is the scheduler's
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
$scheduler = new class implements \Async\Scheduler {
    public function onLaunch(): object { return $this->main ??= new stdClass(); }
    public ?object $main = null;
    public function onShutdown(): void {}
    public function onFiber(\Fiber $fiber): ?object { return null; }
    public SplQueue $tasks;
    public function __construct() { $this->tasks = new SplQueue(); }
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
    public function onDefer(callable $task): void {
        // The queue is owned by the scheduler, not by the engine.
        $this->tasks->enqueue($task);
    }
};

Async\SchedulerHook::register('test', fn () => $scheduler);

Async\SchedulerHook::defer(function (): void {
    echo "task 1\n";

    // Queued while draining: the scheduler decides the semantics; this
    // one drains everything queued before it finishes (classic microtasks).
    Async\SchedulerHook::defer(function (): void {
        echo "task 3 (queued by task 1)\n";
    });
});

Async\SchedulerHook::defer(function (): void {
    echo "task 2\n";
});

// The scheduler drains its own queue on its tick; simulate one here.
while (!$scheduler->tasks->isEmpty()) {
    ($scheduler->tasks->dequeue())();
}

echo "drained\n";
?>
--EXPECT--
task 1
task 2
task 3 (queued by task 1)
drained
