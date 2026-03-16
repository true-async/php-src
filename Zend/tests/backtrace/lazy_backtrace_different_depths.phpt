--TEST--
Lazy backtrace: two exceptions created at different stack depths
--FILE--
<?php
function deep() {
    return new Exception("deep");
}
function mid() {
    $e1 = deep();
    $e2 = new Exception("mid");
    return [$e1, $e2];
}

[$e1, $e2] = mid();
echo "e1: " . $e1->getTraceAsString() . "\n";
echo "e2: " . $e2->getTraceAsString() . "\n";
?>
--EXPECTF--
e1: #0 %s(%d): deep()
#1 %s(%d): mid()
#2 {main}
e2: #0 %s(%d): mid()
#1 {main}
