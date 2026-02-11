--TEST--
Thread: Run requires Closure parameter
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
    $thread->run("not a closure");
} catch (TypeError $e) {
    echo "TypeError caught\n";
}

echo "Done\n";
?>
--EXPECT--
TypeError caught
Done
