--TEST--
Thread: Return value from closure (should not be accessible)
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
    return "This value is lost";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Done
