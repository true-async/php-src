--TEST--
Thread: Test instanceof check
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

echo "Is Thread: " . ($thread instanceof Thread ? 'yes' : 'no') . "\n";
echo "Class: " . get_class($thread) . "\n";
?>
--EXPECT--
Is Thread: yes
Class: Thread
