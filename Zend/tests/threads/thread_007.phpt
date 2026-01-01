--TEST--
Thread: Error on already started thread
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
