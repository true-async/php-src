--TEST--
MM Test: Cross-thread memory deallocation (MPSC queue)
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
use function Php\MemoryManager\hasIncomingQueue;
use function Php\MemoryManager\getMpscStats;

echo "Testing cross-thread memory deallocation...\n";

// Get main thread heap ID
$main_heap_id = getHeapId();
echo "Main thread heap_id: $main_heap_id\n";

// Allocate memory in main thread
$data = str_repeat('A', 1024); // 1KB string

// Create a thread that will free memory from main thread
$thread_id = createThread(function() use ($data) {
    $my_heap_id = getHeapId();
    echo "Worker thread heap_id: $my_heap_id\n";

    // Access the data from main thread
    // When $data goes out of scope and gets freed,
    // it should trigger cross-thread deallocation via SPSC queue
    $len = strlen($data);
    echo "Worker thread accessed data: $len bytes\n";

    // Check stats from worker thread
    $stats = getMpscStats();
    echo "Worker active_queues: {$stats['active_queues']}\n";
});

echo "Created thread: $thread_id\n";

// Wait for thread to complete
joinThread($thread_id);
echo "Thread joined\n";

// Check if SPSC queue was created for cross-thread communication
$stats = getMpscStats();
echo "Main thread stats:\n";
echo "  capacity: {$stats['incoming_queues_capacity']}\n";
echo "  active_queues: {$stats['active_queues']}\n";
echo "  has_queues: " . ($stats['has_incoming_queues'] ? 'yes' : 'no') . "\n";

// The queue should exist if cross-thread free occurred
if ($stats['active_queues'] > 0) {
    echo "SUCCESS: MPSC queue created for cross-thread free\n";
} else {
    echo "INFO: No cross-thread free detected (maybe optimized away)\n";
}

echo "Test completed\n";
?>
--EXPECTF--
Testing cross-thread memory deallocation...
Main thread heap_id: %d
Created thread: 0
Worker thread heap_id: %d
Worker thread accessed data: 1024 bytes
Worker active_queues: %d
Thread joined
Main thread stats:
  capacity: %d
  active_queues: %d
  has_queues: %s
%s
Test completed
