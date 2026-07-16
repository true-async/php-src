--TEST--
End-of-main handover: the finished main is replaced with a fresh main coroutine
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
final class ReplacingScheduler implements Async\Scheduler {
    public array $mains = [];
    public ?object $main = null;
    public function __construct(public readonly Closure $current) {}

    public function onLaunch(): object {
        return $this->main = $this->mains[] = new stdClass();
    }

    public function onShutdown(): void {}
    public function onFiber(Fiber $fiber): ?object { return null; }
    public function onDefer(callable $task): void {}
    public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }

    public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        if (!$fromMain) {
            return null;
        }

        // index.php's main really finished; the flow that runs from here
        // (shutdown functions, destructors) is a different main coroutine.
        $this->main = $this->mains[] = new stdClass();
        echo "handover: main #", count($this->mains), " takes over\n";
        return $this->main;
    }
}

$sched = null;
Async\SchedulerHook::register('test',
    function (Closure $bind, Closure $switch, Closure $current) use (&$sched): ReplacingScheduler {
        return $sched = new ReplacingScheduler($current);
    });

$scriptMain = ($sched->current)();
var_dump($scriptMain === $sched->mains[0]);

register_shutdown_function(function () use ($sched, $scriptMain) {
    $now = ($sched->current)();
    echo "shutdown function runs in a new main: ";
    var_dump($now !== $scriptMain && $now === $sched->mains[1]);
});

echo "end of script\n";
?>
--EXPECT--
bool(true)
end of script
handover: main #2 takes over
shutdown function runs in a new main: bool(true)
handover: main #3 takes over
