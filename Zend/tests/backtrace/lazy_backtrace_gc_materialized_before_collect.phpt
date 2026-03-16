--TEST--
Lazy backtrace: GC collects cycle after trace is materialized
--INI--
zend.enable_gc=1
--FILE--
<?php
class Cyclic {
    public ?Exception $e = null;
    function __destruct() { echo "~Cyclic\n"; }
    function trap() {
        $this->e = new Exception("trapped");
    }
}

$c = new Cyclic();
$c->trap();
// Materialize the trace while the cycle still exists
$trace = $c->e->getTraceAsString();
echo $trace . "\n";
unset($c);
$collected = gc_collect_cycles();
echo "collected: " . ($collected > 0 ? "yes" : "no") . "\n";
?>
--EXPECTF--
#0 %s(%d): Cyclic->trap()
#1 {main}
~Cyclic
collected: no
