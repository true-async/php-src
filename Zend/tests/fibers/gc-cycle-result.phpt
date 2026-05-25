--TEST--
GC can cleanup cycle when fiber result references fiber
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php

$fiber = null;
$fiber = new Fiber(function () use (&$fiber) {
    return new class($fiber) {
        private $fiber;

        public function __construct($fiber) {
            $this->fiber = $fiber;
        }

        public function __destruct() {
            var_dump('DTOR');
        }
    };
});

$fiber->start();

var_dump('COLLECT CYCLES');
gc_collect_cycles();
var_dump('DONE');

var_dump($fiber->isTerminated());

unset($fiber);

var_dump('COLLECT CYCLES');
gc_collect_cycles();

// In TrueAsync, destructors from GC run in a separate coroutine.
// The order of DTOR and "2" is platform-dependent (differs on UNIX vs Windows).
Async\spawn(function () {
    echo "2\n";
});

var_dump('DONE');

?>
--EXPECTF--
string(14) "COLLECT CYCLES"
string(4) "DONE"
bool(true)
string(14) "COLLECT CYCLES"
string(4) "DONE"
string(4) "DTOR"
2