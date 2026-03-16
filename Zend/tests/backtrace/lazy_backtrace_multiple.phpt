--TEST--
Lazy backtrace: multiple exceptions created in the same frame
--FILE--
<?php
function multi() {
    $a = new Exception("first");
    $b = new Exception("second");
    return [$a, $b];
}

[$a, $b] = multi();
echo $a->getTraceAsString() . "\n";
echo $b->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): multi()
#1 {main}
#0 %s(%d): multi()
#1 {main}
