--TEST--
Thread: Run requires Closure parameter
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
    $thread->run("not a closure");
} catch (TypeError $e) {
    echo "TypeError caught\n";
}

echo "Done\n";
?>
--EXPECT--
TypeError caught
Done
