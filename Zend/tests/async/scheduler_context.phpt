--TEST--
Fiber operations on a bound fiber: direct inside the scheduler, routed outside
--FILE--
<?php
$queue = new SplQueue();
$log = [];

Async\SchedulerHook::register('test', [
    Async\SchedulerHook::INTERCEPT_FIBER => fn (Fiber $fiber): object
        => new class($fiber) {
            public function __construct(public readonly Fiber $fiber) {}
        },
    Async\SchedulerHook::ENQUEUE => function (object $coroutine) use ($queue, &$log): bool {
        $log[] = 'enqueue';
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::RESUME => function (object $coroutine, ?Throwable $error) use ($queue, &$log): bool {
        $log[] = 'resume-hook';
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout) use ($queue, &$log): bool {
        while (!$queue->isEmpty()) {
            $fiber = $queue->dequeue()->fiber;

            // Inside a hook this is a DIRECT switch: no hooks re-enter.
            $fiber->isStarted() ? $fiber->resume() : $fiber->start();

            if (!$fromMain) {
                return true;
            }
        }

        return true;
    },
]);

$fiber = new Fiber(function (): string {
    Fiber::suspend('first');
    return 'done';
});

// Application code: operations route through the hooks.
var_dump($fiber->start());
var_dump($fiber->resume());
var_dump($fiber->getReturn());

// The scheduler's direct switches fired no extra hook calls:
echo implode(',', $log), "\n";

// A finished bound fiber cannot be resumed, same rule as classic fibers.
try {
    $fiber->resume();
} catch (FiberError $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
string(5) "first"
NULL
string(4) "done"
enqueue,resume-hook
Cannot resume a fiber that is not suspended
