--TEST--
GC can cleanup cycle when callback references fiber
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php

$ref = new class () {
    public $fiber;

    public function __destruct() {
        var_dump('DTOR');
    }
};

$fiber = new Fiber(function () use ($ref) {
    die('UNREACHABLE');
});

$ref->fiber = $fiber;

$fiber = null;
$ref = null;

var_dump('COLLECT CYCLES');
gc_collect_cycles();

// In TrueAsync, destructors from GC run in a separate coroutine.
// Spawn a coroutine after gc_collect_cycles() to verify the destructor
// runs before the spawned coroutine (GC coroutine has higher priority).
Async\spawn(function () {
    echo "2\n";
});

var_dump('DONE');

?>
--EXPECT--
string(14) "COLLECT CYCLES"
string(4) "DONE"
string(4) "DTOR"
2
