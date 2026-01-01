--TEST--
Thread: Numeric string keys in array
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

$thread->run(function($arr) {
    foreach ($arr as $key => $value) {
        echo "Key: $key, Value: $value, Type: " . gettype($key) . "\n";
    }
}, [['0' => 'zero', '1' => 'one', 'two' => 'TWO']]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Key: 0, Value: zero, Type: integer
Key: 1, Value: one, Type: integer
Key: two, Value: TWO, Type: string
Done
