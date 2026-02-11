--TEST--
Thread: Thread with mixed type arguments
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

$thread->run(function($int, $float, $str, $bool, $null) {
    echo "Int: $int\n";
    echo "Float: $float\n";
    echo "String: $str\n";
    echo "Bool: " . ($bool ? 'true' : 'false') . "\n";
    echo "Null: " . (is_null($null) ? 'null' : 'not null') . "\n";
}, [42, 3.14, "test", true, null]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Int: 42
Float: 3.14
String: test
Bool: true
Null: null
Done
