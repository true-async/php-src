--TEST--
Lazy backtrace: exception with previous exception
--FILE--
<?php
function inner() {
    return new Exception("inner");
}
function outer() {
    $prev = inner();
    return new Exception("outer", 0, $prev);
}

$e = outer();
echo $e->getMessage() . "\n";
echo $e->getTraceAsString() . "\n";
echo "---\n";
echo $e->getPrevious()->getMessage() . "\n";
echo $e->getPrevious()->getTraceAsString() . "\n";
?>
--EXPECTF--
outer
#0 %s(%d): outer()
#1 {main}
---
inner
#0 %s(%d): inner()
#1 %s(%d): outer()
#2 {main}
