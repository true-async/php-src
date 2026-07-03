--TEST--
A fiber adopted by the scheduler runs through the coroutine path
--FILE--
<?php
$queue = new SplQueue();
$log = [];

Async\SchedulerHook::register('test', [
    Async\SchedulerHook::INTERCEPT_FIBER => function (Fiber $fiber) use (&$log): object {
        $log[] = 'intercept';

        // The scheduler defines the coroutine; here it simply remembers
        // which fiber the coroutine drives.
        return new class($fiber) {
            public function __construct(public readonly Fiber $fiber) {}
        };
    },
    Async\SchedulerHook::ENQUEUE => function (object $coroutine) use ($queue, &$log): bool {
        $log[] = 'enqueue';
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::RESUME => function (object $coroutine, ?Throwable $error) use ($queue, &$log): bool {
        $log[] = 'resume';
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout) use ($queue, &$log): bool {
        $log[] = 'suspend';

        while (!$queue->isEmpty()) {
            $fiber = $queue->dequeue()->fiber;
            $fiber->isStarted() ? $fiber->resume() : $fiber->start();

            if (!$fromMain) {
                return true;
            }
        }

        return true;
    },
]);

$fiber = new Fiber(function (int $x): int {
    $y = Fiber::suspend($x + 1);
    return $y * 10;
});

var_dump($fiber->start(5));
var_dump($fiber->isSuspended());
var_dump($fiber->resume(4));
var_dump($fiber->isTerminated());
var_dump($fiber->getReturn());
echo implode(',', $log), "\n";
?>
--EXPECT--
int(6)
bool(true)
NULL
bool(true)
int(40)
intercept,enqueue,suspend,resume,suspend
