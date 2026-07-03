--TEST--
Async\SchedulerHook: hook name constants
--FILE--
<?php
var_dump(Async\SchedulerHook::LAUNCH);
var_dump(Async\SchedulerHook::SHUTDOWN);
var_dump(Async\SchedulerHook::INTERCEPT_FIBER);
var_dump(Async\SchedulerHook::ENQUEUE);
var_dump(Async\SchedulerHook::SUSPEND);
var_dump(Async\SchedulerHook::RESUME);
var_dump(Async\SchedulerHook::CANCEL);
var_dump(Async\SchedulerHook::CONTEXT_FIND);
var_dump(Async\SchedulerHook::CONTEXT_SET);
var_dump(Async\SchedulerHook::CONTEXT_UNSET);
?>
--EXPECT--
string(6) "launch"
string(8) "shutdown"
string(15) "intercept_fiber"
string(17) "enqueue_coroutine"
string(7) "suspend"
string(6) "resume"
string(6) "cancel"
string(12) "context_find"
string(11) "context_set"
string(13) "context_unset"
