--TEST--
Thread: UTF-8 string handling
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

$thread->run(function($str) {
    echo "UTF-8: $str\n";
}, ["Привет 世界 🚀"]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
UTF-8: Привет 世界 🚀
Done
