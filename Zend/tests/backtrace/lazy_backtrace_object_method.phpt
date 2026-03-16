--TEST--
Lazy backtrace: instance method $this is preserved in trace
--FILE--
<?php
class MyObj {
    public string $name;
    function __construct(string $name) { $this->name = $name; }
    function fail() { return new Exception("from " . $this->name); }
}

$obj = new MyObj("test");
$e = $obj->fail();
unset($obj);
$trace = $e->getTrace();
echo $trace[0]['class'] . "\n";
echo $trace[0]['type'] . "\n";
echo $trace[0]['function'] . "\n";
echo $e->getMessage() . "\n";
?>
--EXPECT--
MyObj
->
fail
from test
