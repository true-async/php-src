--TEST--
Lazy backtrace: getTrace() returns correct array structure
--FILE--
<?php
function inner() { return new Exception("test"); }
function outer() { return inner(); }

$e = outer();
$trace = $e->getTrace();

echo count($trace) . "\n";
echo $trace[0]['function'] . "\n";
echo $trace[1]['function'] . "\n";
echo isset($trace[0]['file']) ? "has file\n" : "no file\n";
echo isset($trace[0]['line']) ? "has line\n" : "no line\n";
?>
--EXPECT--
2
inner
outer
has file
has line
