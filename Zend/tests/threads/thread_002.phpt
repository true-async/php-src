--TEST--
Thread: Thread with arguments
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

$thread->run(function($a, $b, $c) {
    echo "Args: $a, $b, $c\n";
}, [1, 2, 3]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Args: 1, 2, 3
Done
