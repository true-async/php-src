--TEST--
Lazy backtrace: GC breaks circular reference where exception holds $this and $this holds exception
--INI--
zend.enable_gc=1
--FILE--
<?php
class Node {
    public ?Exception $exc = null;
    public string $name;
    function __construct(string $name) { $this->name = $name; }
    function __destruct() { echo "~{$this->name}\n"; }
    function createCycle(): void {
        // Exception's backtrace frame holds $this (Node) via ZEND_CALL_RELEASE_THIS
        // Node holds the Exception via $this->exc
        // This creates a cycle: Node -> Exception -> backtrace_frame -> Node
        $this->exc = new Exception("cycle");
    }
}

$n = new Node("N1");
$n->createCycle();
unset($n);
// Node and Exception form a cycle — only GC can collect them
$collected = gc_collect_cycles();
echo "collected: " . ($collected > 0 ? "yes" : "no") . "\n";
echo "done\n";
?>
--EXPECT--
~N1
collected: no
done
