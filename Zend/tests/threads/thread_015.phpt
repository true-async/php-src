--TEST--
Thread: Empty arguments array
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
    echo "No arguments\n";
}, []);

$thread->join();
echo "Done\n";
?>
--EXPECT--
No arguments
Done
