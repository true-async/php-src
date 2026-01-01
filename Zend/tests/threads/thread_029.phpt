--TEST--
Thread: Return value from closure (should not be accessible)
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
    return "This value is lost";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Done
