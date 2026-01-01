--TEST--
Thread: Constructor with bootstrap parameter
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
$thread = new Thread("bootstrap.php");

$thread->run(function() {
    echo "Thread with bootstrap\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Thread with bootstrap
Done
