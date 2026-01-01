--TEST--
Thread: Constructor with bootstrap parameter
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
$thread = new Thread("bootstrap.php");

$thread->run(function() {
    echo "Thread with bootstrap\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Thread with bootstrap
Done
