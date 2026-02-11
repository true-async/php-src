--TEST--
Thread: Closure accessing global scope (should not work)
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
$globalVar = "I am global";

$thread = new Thread();
$thread->run(function() {
    global $globalVar;
    // Global scope is separate in thread
    echo "Global var: " . ($globalVar ?? "undefined") . "\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Global var: undefined
Done
