<?php

/*
 * A scheduler that schedules nothing: the tests drive coroutines directly
 * through the mandate capabilities it captures.
 */
final class SwitchHarness implements Async\Scheduler
{
    public ?object $main = null;

    public function __construct(
        public readonly Closure $bind,
        public readonly Closure $switch,
        public readonly Closure $current,
    ) {}

    public function onLaunch(): object
    {
        return $this->main ??= new stdClass();
    }

    public function onShutdown(): void {}
    public function onFiber(Fiber $fiber): ?object { return null; }
    public function onDefer(callable $task): void {}
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }

    public function onSuspend(bool $fromMain, bool $isBailout): ?object
    {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }

    /** A fresh coroutine with $entry as its body. */
    public function coroutine(Closure $entry): object
    {
        $coroutine = new stdClass();
        ($this->bind)($coroutine, $entry);
        return $coroutine;
    }
}

function register_switch_harness(): SwitchHarness
{
    $scheduler = null;
    Async\SchedulerHook::register('switch-harness',
        function (Closure $bind, Closure $switch, Closure $current) use (&$scheduler): SwitchHarness {
            return $scheduler = new SwitchHarness($bind, $switch, $current);
        });

    return $scheduler;
}
