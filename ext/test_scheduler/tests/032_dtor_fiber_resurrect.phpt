--TEST--
Fibers in destructors 009: Destructor resurrects object, suspends — under test_scheduler
--SKIPIF--
<?php
if (function_exists("TestScheduler\\spawn")) die("skip Fiber::suspend() in destructor is not supported under the async scheduler");
?>
--EXTENSIONS--
test_scheduler
--INI--
test_scheduler.enable=1
--FILE--
<?php

register_shutdown_function(function () {
    printf("Shutdown\n");
});

class Cycle {
    public $self;
    public function __construct() {
        $this->self = $this;
    }
    public function __destruct() {
        global $ref, $f2;
        $ref = $this;
        $f2 = Fiber::getCurrent();
        Fiber::suspend();
    }
}

$f = new Fiber(function () {
    global $weakRef;
    $weakRef = WeakReference::create(new Cycle());
    gc_collect_cycles();
});

$f->start();
var_dump((bool) $weakRef->get());
gc_collect_cycles();
$f2->resume();
gc_collect_cycles();
var_dump((bool) $weakRef->get());
?>
--EXPECT--
bool(true)
bool(true)
Shutdown
