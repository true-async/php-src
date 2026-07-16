--TEST--
Async\get_context(): the main coroutine's store; string and object keys
--EXTENSIONS--
async_scheduler_hook
--SKIPIF--
<?php
if (Async\SchedulerHook::getModule() !== null) die('skip a C scheduler occupies the slot');
?>
--FILE--
<?php
require __DIR__ . '/mini_scheduler.inc';

// Without a scheduler there is no coroutine at all — and no context.
try {
    Async\get_context();
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

register_mini_scheduler();

$context = Async\get_context();
var_dump($context instanceof Async\Context);

// The same store on every call.
var_dump(Async\get_context() === $context);

// String keys: find/has/set/unset, set() chains.
var_dump($context->has('request.id'));
var_dump($context->set('request.id', 'r-1')->set('locale', 'en')->find('request.id'));
var_dump($context->find('locale'));
var_dump($context->has('request.id'));
var_dump($context->unset('request.id'));
var_dump($context->unset('request.id'));
var_dump($context->find('request.id'));

// A stored null is distinguishable from an absent key.
$context->set('nullable', null);
var_dump($context->find('nullable'), $context->has('nullable'));

// Object keys.
$key = new stdClass();
$context->set($key, 'per-object');
var_dump($context->find($key), $context->has($key));
var_dump($context->has(new stdClass()));
var_dump($context->unset($key));

?>
--EXPECT--
Async\get_context(): no coroutine is running
bool(true)
bool(true)
bool(false)
string(3) "r-1"
string(2) "en"
bool(true)
bool(true)
bool(false)
NULL
NULL
bool(true)
string(10) "per-object"
bool(true)
bool(false)
bool(true)
