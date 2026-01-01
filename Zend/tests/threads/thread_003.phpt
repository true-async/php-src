--TEST--
Thread: Thread with string arguments
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

$thread->run(function($name, $msg) {
    echo "$name says: $msg\n";
}, ["Alice", "Hello World"]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Alice says: Hello World
Done
