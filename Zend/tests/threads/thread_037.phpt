--TEST--
Thread: Test instanceof check
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

echo "Is Thread: " . ($thread instanceof Thread ? 'yes' : 'no') . "\n";
echo "Class: " . get_class($thread) . "\n";
?>
--EXPECT--
Is Thread: yes
Class: Thread
