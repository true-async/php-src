--TEST--
Lazy backtrace: GC handles circular reference with exception holding $this
--FILE--
<?php
class Circular {
    public ?Exception $exc = null;
    function create() {
        $this->exc = new Exception("cycle");
    }
}

$c = new Circular();
$c->create();
unset($c);
gc_collect_cycles();
echo "OK";
?>
--EXPECT--
OK
