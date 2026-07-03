--TEST--
An uncaught exception in a managed fiber surfaces at the start()/resume() caller
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

$fiber = new Fiber(function (): void {
    throw new RuntimeException('escaped');
});

try {
    $fiber->start();
} catch (RuntimeException $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

var_dump($fiber->isTerminated());
?>
--EXPECT--
caught: escaped
bool(true)
