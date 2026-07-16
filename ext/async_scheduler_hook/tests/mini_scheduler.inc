<?php

/*
 * The RFC's MiniScheduler: everything is keyed by the scheduler's own
 * coroutine objects — the main one (onLaunch and every end-of-main
 * replacement), adopted fibers (the engine runs their bodies itself) and the
 * scheduler's own (a body bound through the bindEntry capability) all switch
 * through the one switchTo capability.
 */
final class MiniScheduler implements Async\Scheduler
{
    public ?object $main = null;
    public ?object $lastAdopted = null;
    public array $adopted = [];
    public SplQueue $queue;
    public array $log = [];
    private ?object $handoff = null;

    public function __construct(
        private readonly Closure $bindEntry,
        private readonly Closure $switchTo,
        private readonly Closure $current,
    ) {
        $this->queue = new SplQueue();
    }

    public function onLaunch(): object
    {
        return $this->main ??= new stdClass();
    }

    public function onShutdown(): void {}

    public function onDefer(callable $task): void {}

    public function onFiber(Fiber $fiber): ?object
    {
        $this->log[] = 'intercept';
        return $this->lastAdopted = $this->adopted[] = new stdClass();
    }

    // spawn() is the scheduler's own API, not part of the hook set.
    public function spawn(Closure $entry): object
    {
        $coroutine = new stdClass();
        ($this->bindEntry)($coroutine, $entry);
        $this->onEnqueue($coroutine);
        return $coroutine;
    }

    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool
    {
        $this->log[] = 'enqueue';
        $this->queue->enqueue([$coroutine, $error]);
        return true;
    }

    public function onSuspend(bool $fromMain, bool $isBailout): ?object
    {
        $this->log[] = 'suspend';
        $self = ($this->current)();

        while (!$this->queue->isEmpty()) {
            [$next, $error] = $this->queue->dequeue();

            if ($next === $self) {
                // The end-of-main handover: the finished main cannot run
                // again, drop its stale wakes and keep draining.
                if ($fromMain) {
                    continue;
                }
                // A self-resume: the flow yielded and its own turn came. A
                // delivered error surfaces at its suspension point.
                if ($error !== null) {
                    throw $error;
                }
                break;
            }

            $this->handoff = $next;    // a deliberate wake: its turn came
            ($this->switchTo)($next, null, $error);

            if ($this->handoff === $self) {
                break;                 // this flow was dequeued while it was
            }                          // scheduling: stop and resume it
        }

        $this->handoff = null;

        // The script's main coroutine really finished: whatever runs after
        // the drain (shutdown functions, destructors) is a NEW main.
        if ($fromMain) {
            return $this->main = new stdClass();
        }

        return $self;
    }
}

function register_mini_scheduler(): MiniScheduler
{
    $scheduler = null;
    Async\SchedulerHook::register('mini',
        function (Closure $bindEntry, Closure $switchTo, Closure $current) use (&$scheduler): MiniScheduler {
            return $scheduler = new MiniScheduler($bindEntry, $switchTo, $current);
        });

    return $scheduler;
}
