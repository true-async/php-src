--TEST--
Thread: Thread with indexed and associative array keys
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

$thread->run(function($arr) {
    echo "Index 0: {$arr[0]}\n";
    echo "Index 1: {$arr[1]}\n";
    echo "Key 'name': {$arr['name']}\n";
    echo "Key 'age': {$arr['age']}\n";
}, [[0 => 'first', 1 => 'second', 'name' => 'John', 'age' => 30]]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Index 0: first
Index 1: second
Key 'name': John
Key 'age': 30
Done
