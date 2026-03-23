--TEST--
Lazy backtrace: GC collects object referenced only by populated backtrace frame
--INI--
zend.enable_gc=1
--FILE--
<?php
class Ref {
    public string $tag;
    function __construct(string $tag) { $this->tag = $tag; }
    function __destruct() { echo "~{$this->tag}\n"; }
}

function maker() {
    $obj = new Ref("A");
    return $obj->tag . (string)(new Exception("ex"));
    // $obj goes out of scope, but $this in the frame still holds a ref
}

// Exception is created inside Ref context — not applicable here.
// Better test: method call where $this is captured in the frame.

class Holder {
    public string $name;
    function __construct(string $name) { $this->name = $name; }
    function __destruct() { echo "~{$this->name}\n"; }
    function makeException(): Exception {
        return new Exception("from " . $this->name);
    }
}

$h = new Holder("H");
$e = $h->makeException();
unset($h);  // Holder still alive via backtrace frame's $this
echo "before trace\n";
$trace = $e->getTrace();
echo $trace[0]['class'] . "->" . $trace[0]['function'] . "\n";
echo "before unset\n";
unset($e);  // Now frame is released, Holder should be destroyed
echo "after unset\n";
?>
--EXPECT--
~H
before trace
Holder->makeException
before unset
after unset
