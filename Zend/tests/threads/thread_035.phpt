--TEST--
Thread: Closure with multiple use variables
--SKIPIF--
<?php
if (!Thread::isSupported()) {
    die('skip Thread support not available (requires ZTS build)');
}
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
$thread = new Thread();
$a = "first";
$b = "second";
$c = 42;

$thread->run(function() use ($a, $b, $c) {
    echo "A: $a, B: $b, C: $c\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
A: first, B: second, C: 42
Done
