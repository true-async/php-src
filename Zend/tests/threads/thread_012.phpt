--TEST--
Thread: Constructor without bootstrap parameter
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
    echo "Thread without bootstrap\n";
});

$thread->join();
echo "Done\n";
?>
--EXPECT--
Thread without bootstrap
Done
