--TEST--
Thread: Error on already started thread
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
    echo "First run\n";
});

try {
    $thread->run(function() {
        echo "Second run\n";
    });
} catch (Exception $e) {
    echo "Exception: " . $e->getMessage() . "\n";
}

$thread->join();
echo "Done\n";
?>
--EXPECT--
Exception: Thread already started
First run
Done
