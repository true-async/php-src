--TEST--
Lazy backtrace: trace is correct after all frames are destroyed
--FILE--
<?php
function c() { return new Exception("test"); }
function b() { return c(); }
function a() { return b(); }

$e = a();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): c()
#1 %s(%d): b()
#2 %s(%d): a()
#3 {main}
