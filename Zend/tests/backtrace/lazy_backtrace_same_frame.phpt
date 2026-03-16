--TEST--
Lazy backtrace: getTrace called in the same frame where exception was created (no leave)
--FILE--
<?php
function foo() {
    $e = new Exception("here");
    return $e->getTrace();
}

$trace = foo();
echo $trace[0]['function'] . "\n";
echo count($trace) . "\n";
?>
--EXPECT--
foo
1
