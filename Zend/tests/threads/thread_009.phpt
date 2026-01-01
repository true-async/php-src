--TEST--
Thread: Error on kill not started thread
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

try {
    $thread->kill();
} catch (Exception $e) {
    echo "Exception: " . $e->getMessage() . "\n";
}

echo "Done\n";
?>
--EXPECT--
Exception: Thread not started
Done
