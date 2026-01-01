--TEST--
Thread: String with special characters
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
    echo "Special chars: $str\n";
}, ["Hello\nWorld\t!\r\n"]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Special chars: Hello
World	!

Done
