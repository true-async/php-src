--TEST--
Thread: Kill thread execution
--EXTENSIONS--
zts
--SKIPIF--
<?php
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
if (PHP_OS_FAMILY === 'Windows') {
    die('skip Test unstable on Windows');
}
?>
--FILE--
<?php
$thread = new Thread();

$thread->run(function() {
    while (true) {
        // Infinite loop
    }
});

// Give thread time to start
usleep(100000);

$thread->kill();
echo "Thread killed\n";
?>
--EXPECT--
Thread killed
