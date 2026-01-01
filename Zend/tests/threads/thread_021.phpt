--TEST--
Thread: Args parameter must be array
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
    $thread->run(function() {}, "not an array");
} catch (TypeError $e) {
    echo "TypeError caught\n";
}

echo "Done\n";
?>
--EXPECT--
TypeError caught
Done
