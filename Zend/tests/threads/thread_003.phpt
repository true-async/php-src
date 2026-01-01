--TEST--
Thread: Thread with string arguments
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

$thread->run(function($name, $msg) {
    echo "$name says: $msg\n";
}, ["Alice", "Hello World"]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Alice says: Hello World
Done
