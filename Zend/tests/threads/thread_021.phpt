--TEST--
Thread: Args parameter must be array
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
    $thread->run(function() {}, "not an array");
} catch (TypeError $e) {
    echo "TypeError caught\n";
}

echo "Done\n";
?>
--EXPECT--
TypeError caught
Done
