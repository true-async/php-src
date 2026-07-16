--TEST--
The register() factory receives the bindEntry/switchTo/currentCoroutine mandate — and nobody else does
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
final class MandateScheduler implements Async\Scheduler {
    public function onLaunch(): object { return $this->main ??= new stdClass(); }
    public ?object $main = null;
    public function __construct(
        public readonly Closure $bind,     // bindEntry(object $coroutine, callable $entry): void
        public readonly Closure $switch,   // switchTo(object $coroutine, mixed $value = null, ?Throwable $error = null): mixed
        public readonly Closure $current,  // currentCoroutine(): ?object
    ) {}

    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
    public function onShutdown(): void {}
    public function onFiber(Fiber $fiber): ?object { return null; }
    public function onDefer(callable $task): void {}
}

$sched = null;
Async\SchedulerHook::register('test',
    function (Closure $bind, Closure $switch, Closure $current) use (&$sched): MandateScheduler {
        // The scheduler is created in a valid state: the mandate arrives
        // through the constructor, not through a later hook.
        return $sched = new MandateScheduler($bind, $switch, $current);
    });

// bindEntry gives one of the scheduler's coroutines a body; switchTo drives it.
$c = new stdClass();
($sched->bind)($c, function (): void { echo "  in coroutine\n"; });
($sched->switch)($c);

// currentCoroutine reports the coroutine the engine records: the main one,
// minted by onLaunch() before the script ran.
var_dump(($sched->current)() === $sched->main);

// The capabilities are closures over engine internals: no public class or
// function exposes them.
var_dump(class_exists('Async\\Continuation'));
?>
--EXPECT--
  in coroutine
bool(true)
bool(false)
