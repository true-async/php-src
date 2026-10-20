--TEST--
GC scan of a suspended fiber's stack stops at the fiber's bottom frame
--FILE--
<?php
class Cycle {
    public $self;
    public function __construct() { $this->self = $this; }
    public function __destruct() { echo "Cycle::__destruct\n"; }
}

$fiber = new Fiber(function () {
    new Cycle;
    gc_collect_cycles();
    Fiber::suspend();
    echo "resumed\n";
});
$fiber->start();
echo "main\n";
$fiber->resume();
echo "end\n";
?>
--EXPECT--
Cycle::__destruct
main
resumed
end
