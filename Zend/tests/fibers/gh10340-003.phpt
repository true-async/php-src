--TEST--
Bug GH-10340 003 (Assertion in zend_fiber_object_gc())
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
    $c = new C();
    $y = 'a';
    $$y = Fiber::getCurrent();
    Fiber::suspend();
}

$fiber = new Fiber(function() {
    get_defined_vars();
    f();
});

$fiber->start();

print "1\n";

$fiber = null;
gc_collect_cycles();

// In TrueAsync, destructors from GC run in a separate coroutine.
Async\spawn(function () {
    echo "3\n";
});

print "2\n";
?>
==DONE==
--EXPECT--
1
2
==DONE==
C::__destruct
3
