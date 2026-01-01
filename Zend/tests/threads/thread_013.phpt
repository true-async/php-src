--TEST--
Thread: Closure with use variables (copied to thread)
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
$message = "Hello from parent";

$thread->run(function() use ($message) {
    echo "$message\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Hello from parent
Done
