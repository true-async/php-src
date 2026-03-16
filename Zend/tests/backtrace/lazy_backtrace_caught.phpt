--TEST--
Lazy backtrace: trace is correct when exception is caught in caller
--FILE--
<?php
function thrower() {
    throw new Exception("caught");
}
function catcher() {
    try {
        thrower();
    } catch (Exception $e) {
        return $e;
    }
}

$e = catcher();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): thrower()
#1 %s(%d): catcher()
#2 {main}
