--TEST--
Lazy backtrace: exception created and discarded without reading trace
--FILE--
<?php
function make() { return new Exception("unused"); }

$e = make();
echo $e->getMessage() . "\n";
// Intentionally NOT calling getTrace()
unset($e);
echo "OK";
?>
--EXPECT--
unused
OK
