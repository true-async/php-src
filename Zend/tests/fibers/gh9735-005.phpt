--TEST--
Bug GH-9735 005 (Fiber stack variables do not participate in cycle collector)
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php

class C {
    public function __destruct() {
        echo __METHOD__, "\n";
    }
}

function f() {
    Fiber::suspend();
}

$fiber = new Fiber(function () {
    $c = new C();

    $fiber = Fiber::getCurrent();

    // Force symbol table
    get_defined_vars();

    f();
});

print "1\n";

$fiber->start();
gc_collect_cycles();

$fiber = null;
gc_collect_cycles();

// In TrueAsync, destructors from GC run in a separate coroutine.
// Spawn a coroutine after gc_collect_cycles() to verify the destructor
// runs before the spawned coroutine (GC coroutine has higher priority).
Async\spawn(function () {
    echo "2\n";
});

print "3\n";

?>
--EXPECTF--
1
3
C::__destruct
2
