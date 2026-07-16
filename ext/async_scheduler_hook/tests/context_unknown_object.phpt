--TEST--
Async\get_context(): an object the engine does not know as a coroutine is a ValueError
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php

try {
    Async\get_context(new stdClass());
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Async\get_context(): Argument #1 ($coroutine) must be a coroutine object known to the scheduler
