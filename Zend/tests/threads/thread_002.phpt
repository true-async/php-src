--TEST--
Thread: Thread with arguments
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

$thread->run(function($a, $b, $c) {
    echo "Args: $a, $b, $c\n";
}, [1, 2, 3]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Args: 1, 2, 3
Done
