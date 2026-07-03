--TEST--
Microtasks: defer() forwards to the scheduler's DEFER hook; the queue is the scheduler's
--FILE--
<?php
$tasks = new SplQueue();

Async\SchedulerHook::register('test', [
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout): bool {
        return true;
    },
    Async\SchedulerHook::DEFER => function (callable $task) use ($tasks): bool {
        // The queue is owned by the scheduler, not by the engine.
        $tasks->enqueue($task);
        return true;
    },
]);

Async\SchedulerHook::defer(function () use ($tasks): void {
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
while (!$tasks->isEmpty()) {
    ($tasks->dequeue())();
}

echo "drained\n";
?>
--EXPECT--
task 1
task 2
task 3 (queued by task 1)
drained
