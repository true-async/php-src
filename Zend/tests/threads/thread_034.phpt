--TEST--
Thread: Sequential thread execution
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
$thread1 = new Thread();
$thread1->run(function() {
    echo "Thread 1\n";
});
$thread1->join();

$thread2 = new Thread();
$thread2->run(function() {
    echo "Thread 2\n";
});
$thread2->join();

$thread3 = new Thread();
$thread3->run(function() {
    echo "Thread 3\n";
});
$thread3->join();

echo "All done\n";
?>
--EXPECT--
Thread 1
Thread 2
Thread 3
All done
