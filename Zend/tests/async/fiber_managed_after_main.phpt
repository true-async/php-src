--TEST--
After-main handover runs deferred managed coroutines; abandoned ones die cleanly
--FILE--
<?php
$queue = new SplQueue();

Async\SchedulerHook::register('test', [
    Async\SchedulerHook::INTERCEPT_FIBER => fn (Fiber $fiber): object
        => new class($fiber) {
            public function __construct(public readonly Fiber $fiber) {}
        },
    Async\SchedulerHook::ENQUEUE => function (object $coroutine) use ($queue): bool {
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout) use ($queue): bool {
        // This scheduler defers everything: nothing runs until after main.
        if (!$fromMain) {
            return true;
        }

        while (!$queue->isEmpty()) {
            $fiber = $queue->dequeue()->fiber;
            $fiber->isStarted() ? $fiber->resume() : $fiber->start();
        }

        return true;
    },
]);

$fiber = new Fiber(function (): void {
    echo "ran after main\n";
    Fiber::suspend();
    echo "never printed: nobody resumes\n";
});

// The deferring scheduler does not run the fiber here.
var_dump($fiber->start());
var_dump($fiber->isStarted());

echo "end of script\n";
// After-main handover: the scheduler drains its queue, the fiber runs and
// suspends. Nobody resumes it, so it is destroyed at shutdown without
// completing; the second echo never happens.
?>
--EXPECT--
NULL
bool(false)
end of script
ran after main
