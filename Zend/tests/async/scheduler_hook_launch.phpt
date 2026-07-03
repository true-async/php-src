--TEST--
Async\SchedulerHook: launch runs immediately at PHP registration
--FILE--
<?php
$launched = false;

Async\SchedulerHook::register('test', [
    Async\SchedulerHook::LAUNCH => function () use (&$launched): bool {
        $launched = true;
        return true;
    },
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout): bool {
        return true;
    },
]);

// The engine launch point has already passed by the time userland runs,
// so a PHP scheduler is launched synchronously inside register().
var_dump($launched);
?>
--EXPECT--
bool(true)
