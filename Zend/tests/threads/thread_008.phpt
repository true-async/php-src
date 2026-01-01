--TEST--
Thread: Error on join not started thread
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

try {
    $thread->join();
} catch (Exception $e) {
    echo "Exception: " . $e->getMessage() . "\n";
}

echo "Done\n";
?>
--EXPECT--
Exception: Thread not started
Done
