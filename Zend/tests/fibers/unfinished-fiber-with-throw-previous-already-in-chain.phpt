--TEST--
Exception thrown in finally of a fiber destroyed during unwinding that already has the pending exception in its previous chain
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php
function f() {
    $pending = new RuntimeException("pending");
    $fiber = new Fiber(function () use ($pending) {
        try {
            Fiber::suspend();
        } finally {
            throw new LogicException("thrown", 0, $pending);
        }
    });
    $fiber->start();
    throw $pending;
}

try {
    f();
} catch (Throwable $t) {
    for (; $t !== null; $t = $t->getPrevious()) {
        echo $t->getMessage(), "\n";
    }
}
?>
--EXPECTF--
pending

Fatal error: Uncaught RuntimeException: pending in %s:%d
Stack trace:
#0 %s(%d): f()
#1 {main}

Next LogicException: thrown in %s:%d
Stack trace:
#0 [internal function]: {closure:f():%d}()
#1 {main}
  thrown in %s on line %d
