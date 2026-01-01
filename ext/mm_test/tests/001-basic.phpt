--TEST--
MM Test: Basic extension functionality
--EXTENSIONS--
mm_test
--FILE--
<?php
use function Php\MemoryManager\getHeapId;
use function Php\MemoryManager\getMpscStats;

// Test basic functions
$heap_id = getHeapId();
var_dump($heap_id >= 0);

$stats = getMpscStats();
var_dump(isset($stats['heap_id']));
var_dump($stats['heap_id'] === $heap_id);

echo "OK\n";
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
OK
