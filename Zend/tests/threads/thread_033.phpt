--TEST--
Thread: Zero and empty values
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

$thread->run(function($zero, $emptyStr, $emptyArr, $null) {
    echo "Zero: " . var_export($zero, true) . "\n";
    echo "Empty string: " . var_export($emptyStr, true) . "\n";
    echo "Empty array: " . var_export($emptyArr, true) . "\n";
    echo "Null: " . var_export($null, true) . "\n";
}, [0, "", [], null]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Zero: 0
Empty string: ''
Empty array: array (
)
Null: NULL
Done
