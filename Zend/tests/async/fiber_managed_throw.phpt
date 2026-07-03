--TEST--
Fiber::throw() on a managed fiber delivers the exception at the suspension point
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
    Async\SchedulerHook::RESUME => function (object $coroutine, ?Throwable $error) use ($queue): bool {
        echo "resume hook error: ", $error === null ? 'none' : get_class($error), "\n";
        $queue->enqueue($coroutine);
        return true;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout) use ($queue): bool {
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

$fiber = new Fiber(function (): string {
    try {
        Fiber::suspend('waiting');
    } catch (RuntimeException $e) {
        return 'caught: ' . $e->getMessage();
    }

    return 'not reached';
});

var_dump($fiber->start());
$fiber->throw(new RuntimeException('boom'));
var_dump($fiber->getReturn());
?>
--EXPECT--
string(7) "waiting"
resume hook error: RuntimeException
string(12) "caught: boom"
