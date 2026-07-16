--TEST--
Async\SchedulerHook: the factory runs synchronously inside register()
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
$order = [];

$factory = function () use (&$order): \Async\Scheduler {
    $order[] = 'factory';
    return new class implements \Async\Scheduler {
        public function onLaunch(): object { return $this->main ??= new stdClass(); }
        public ?object $main = null;
        public function onShutdown(): void {}
        public function onFiber(\Fiber $fiber): ?object { return null; }
        public function onDefer(callable $task): void {}
        public function onEnqueue(object $coroutine, ?Throwable $error = null): bool { return true; }
        public function onSuspend(bool $fromMain, bool $isBailout): ?object {
        return $fromMain ? ($this->main = new stdClass()) : null;
    }
    };
};

$order[] = 'before register';
Async\SchedulerHook::register('test', $factory);
$order[] = 'after register';

// The engine launch point has already passed by the time userland runs, so
// the factory (the launch moment for a PHP scheduler) runs synchronously
// inside register().
echo implode("\n", $order), "\n";
?>
--EXPECT--
before register
factory
after register
