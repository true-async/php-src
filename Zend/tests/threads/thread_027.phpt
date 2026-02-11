--TEST--
Thread: UTF-8 string handling
--SKIPIF--
<?php
if (!Thread::isSupported()) {
    die('skip Thread support not available (requires ZTS build)');
}
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
