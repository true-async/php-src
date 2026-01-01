--TEST--
Thread: Closure with use variables (copied to thread)
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
