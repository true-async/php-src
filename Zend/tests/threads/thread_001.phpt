--TEST--
Thread: Basic thread creation and execution
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
$result = 0;

$thread->run(function() {
    echo "Thread executed\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Thread executed
Done
