--TEST--
Async\SchedulerHook::register activates and getModule() reports the driver
--FILE--
<?php
var_dump(Async\SchedulerHook::getModule());

$ok = Async\SchedulerHook::register('my-driver', [
    Async\SchedulerHook::SUSPEND => function (bool $fromMain, bool $isBailout): bool {
        return true;
    },
]);

var_dump($ok);
var_dump(Async\SchedulerHook::getModule());
?>
--EXPECT--
NULL
bool(true)
string(9) "my-driver"
