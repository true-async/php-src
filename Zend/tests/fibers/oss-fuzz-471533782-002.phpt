--TEST--
OSS-Fuzz #471533782: Infinite loop in GC destructor fiber
--SKIPIF--
<?php if (extension_loaded("true_async")) die("skip TrueAsync does not use gc_destructor_fiber"); ?>
--FILE--
<?php

class Cycle {
    public $self;
    public function __construct() {
        $this->self = $this;
    }
    public function __destruct() {
        try {
            Fiber::suspend();
        } finally {
            Fiber::suspend();
        }
    }
}

$f = new Fiber(function () {
    new Cycle();
    gc_collect_cycles();
});
$f->start();

?>
--EXPECTF--
Fatal error: Uncaught FiberError: Cannot suspend in a force-closed fiber in %s:%d
Stack trace:
#0 %s(%d): Fiber::suspend()
#1 [internal function]: Cycle->__destruct()
#2 [internal function]: gc_destructor_fiber()
#3 {main}
  thrown in %s on line %d
