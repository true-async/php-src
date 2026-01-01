--TEST--
Thread: Large array deep copy
--EXTENSIONS--
zts
--SKIPIF--
<?php
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
$thread = new Thread();
$largeArray = range(1, 100);

$thread->run(function($arr) {
    echo "Array size: " . count($arr) . "\n";
    echo "Sum: " . array_sum($arr) . "\n";
}, [$largeArray]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Array size: 100
Sum: 5050
Done
