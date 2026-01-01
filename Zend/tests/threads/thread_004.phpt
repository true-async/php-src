--TEST--
Thread: Thread with array arguments
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

$thread->run(function($arr) {
    echo "Array elements: ";
    foreach ($arr as $item) {
        echo "$item ";
    }
    echo "\n";
}, [[1, 2, 3, 4, 5]]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Array elements: 1 2 3 4 5
Done
