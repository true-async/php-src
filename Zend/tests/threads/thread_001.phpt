--TEST--
Thread: Basic thread creation and execution
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
