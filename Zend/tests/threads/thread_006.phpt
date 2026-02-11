--TEST--
Thread: Closure with static variables
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

$counter = function() {
    static $count = 0;
    $count++;
    echo "Count: $count\n";
};

$thread->run($counter);
$thread->join();

echo "Done\n";
?>
--EXPECT--
Count: 1
Done
