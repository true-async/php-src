--TEST--
Thread: Multiple threads execution
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
$threads = [];

for ($i = 1; $i <= 3; $i++) {
    $thread = new Thread();
    $thread->run(function($id) {
        echo "Thread $id executing\n";
    }, [$i]);
    $threads[] = $thread;
}

foreach ($threads as $thread) {
    $thread->join();
}

echo "All threads completed\n";
?>
--EXPECTF--
Thread %d executing
Thread %d executing
Thread %d executing
All threads completed
