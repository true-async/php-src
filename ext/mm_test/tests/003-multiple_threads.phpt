--TEST--
MM Test: Multiple threads freeing to same owner (MPSC)
--EXTENSIONS--
mm_test
--SKIPIF--
<?php
if (!function_exists('Php\\MemoryManager\\createThread')) {
    die('skip Requires ZTS build');
}
?>
--FILE--
<?php
use function Php\MemoryManager\getHeapId;
use function Php\MemoryManager\createThread;
use function Php\MemoryManager\joinThread;
use function Php\MemoryManager\getMpscStats;

echo "Testing multiple senders to single owner...\n";

$main_heap_id = getHeapId();
echo "Main heap_id: $main_heap_id\n";

// Create multiple data objects
$data1 = str_repeat('X', 512);
$data2 = str_repeat('Y', 512);
$data3 = str_repeat('Z', 512);

// Create multiple threads that will access and free main thread's memory
$threads = [];

$threads[] = createThread(function() use ($data1) {
    echo "Thread 1 heap_id: " . getHeapId() . "\n";
    strlen($data1); // Access data
});

$threads[] = createThread(function() use ($data2) {
    echo "Thread 2 heap_id: " . getHeapId() . "\n";
    strlen($data2); // Access data
});

$threads[] = createThread(function() use ($data3) {
    echo "Thread 3 heap_id: " . getHeapId() . "\n";
    strlen($data3); // Access data
});

echo "Created " . count($threads) . " threads\n";

// Join all threads
foreach ($threads as $tid) {
    joinThread($tid);
}
echo "All threads joined\n";

// Check MPSC stats
$stats = getMpscStats();
echo "Final stats:\n";
echo "  capacity: {$stats['incoming_queues_capacity']}\n";
echo "  active_queues: {$stats['active_queues']}\n";

echo "Test completed\n";
?>
--EXPECTF--
Testing multiple senders to single owner...
Main heap_id: %d
Thread 1 heap_id: %d
Thread 2 heap_id: %d
Thread 3 heap_id: %d
Created 3 threads
All threads joined
Final stats:
  capacity: %d
  active_queues: %d
Test completed
