--TEST--
Thread: Thread exception handling
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
    throw new Exception("Thread exception");
});

$thread->join();
echo "This should not be printed\n";
?>
--EXPECTF--
Fatal error: Uncaught Exception: Thread exception in %s
