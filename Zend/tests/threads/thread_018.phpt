--TEST--
Thread: Thread exception handling
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

$thread->run(function() {
    throw new Exception("Thread exception");
});

$thread->join();
echo "This should not be printed\n";
?>
--EXPECTF--
Fatal error: Uncaught Exception: Thread exception in %s
