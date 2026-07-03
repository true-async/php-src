--TEST--
GC destructor phase: the hook brackets the engine run and awaits spawned work
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
        // Defer: the queue is drained by the GC hook (and after main).
        if (!$fromMain) {
            return true;
        }

        while (!$queue->isEmpty()) {
            $fiber = $queue->dequeue()->fiber;
            $fiber->isStarted() ? $fiber->resume() : $fiber->start();
        }

        return true;
    },
    Async\SchedulerHook::GC_DESTRUCTORS => function (callable $run) use ($queue): bool {
        echo "hook: before\n";

        $run();     // the engine calls every pending destructor

        // Await everything the destructors spawned (the "scope" logic:
        // membership is the scheduler's own bookkeeping).
        while (!$queue->isEmpty()) {
            $fiber = $queue->dequeue()->fiber;
            $fiber->isStarted() ? $fiber->resume() : $fiber->start();
        }

        echo "hook: after\n";
        return true;
    },
]);

class Node
{
    public ?Node $other = null;

    public function __destruct()
    {
        echo "destructor\n";

        // The destructor spawns concurrent work; the hook must wait for it.
        $fiber = new Fiber(function (): void {
            echo "spawned by destructor\n";
        });
        $fiber->start();
    }
}

// A garbage cycle: collected only by the GC.
$a = new Node();
$b = new Node();
$a->other = $b;
$b->other = $a;
unset($a, $b);

// The GC reruns after the destructor phase: the finished fiber/coroutine
// pairs collected there carry internal destructors, so the hook brackets
// a second (empty) phase.
$collected = gc_collect_cycles();
var_dump($collected > 0);
echo "end\n";
?>
--EXPECT--
hook: before
destructor
destructor
spawned by destructor
spawned by destructor
hook: after
hook: before
hook: after
bool(true)
end
