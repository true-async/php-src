--TEST--
Lazy backtrace: exception created in function called from generator
--FILE--
<?php
function inner() { return new Exception("deep"); }
function gen() { yield inner(); }

$g = gen();
$e = $g->current();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): inner()
#1 [internal function]: gen()
#2 %s(%d): Generator->current()
#3 {main}
