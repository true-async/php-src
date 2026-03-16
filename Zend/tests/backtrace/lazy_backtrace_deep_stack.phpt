--TEST--
Lazy backtrace: deep recursion produces correct trace
--FILE--
<?php
function recurse($n) {
    if ($n === 0) return new Exception("deep");
    return recurse($n - 1);
}

$e = recurse(50);
$trace = $e->getTrace();
echo count($trace) . "\n";
echo $trace[0]['function'] . "\n";
echo $trace[50]['function'] . "\n";
?>
--EXPECT--
51
recurse
recurse
