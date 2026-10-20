--TEST--
A GC destructor that throws in the destructors coroutine lets the GC run finish
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php
class Cycle {
    public $self;
    public function __construct() { $this->self = $this; }
    public function __destruct() {
        echo "Cycle::__destruct\n";
        throw new Exception('Cycle::__destruct');
    }
}

Async\spawn(function () {
    new Cycle;
    gc_collect_cycles();
    echo "after gc_collect_cycles\n";
});
echo "end of main\n";
?>
--EXPECTF--
end of main
after gc_collect_cycles
Cycle::__destruct

Fatal error: Uncaught Exception: Cycle::__destruct in %s:%d
Stack trace:
#0 [internal function]: Cycle->__destruct()
#1 {main}
  thrown in %s on line %d
