--TEST--
PDO PgSQL concurrent query - parallel execution timing verification
--EXTENSIONS--
pdo_pgsql
true_async
--SKIPIF--
<?php
require __DIR__ . '/config.inc';
require dirname(__DIR__, 2) . '/pdo/tests/pdo_test.inc';
PDOTest::skip();
?>
--FILE--
<?php

use function Async\spawn;
use function Async\await_all;

require_once __DIR__ . "/config.inc";

$dsn = $config['ENV']['PDOTEST_DSN'];
$db = new PDO($dsn);
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

echo "Testing parallel query execution timing...\n";

$start_time = microtime(true);

// Launch 3 queries in parallel, each sleeping 0.3 seconds
$coroutines = [];

for ($i = 0; $i < 3; $i++) {
    $coroutines[] = spawn(function() use ($dsn, $i) {
        $db = new PDO($dsn);
        $stmt = $db->query("SELECT pg_sleep(0.3), {$i} as idx");
        $row = $stmt->fetch(PDO::FETCH_ASSOC);
        return (int)$row['idx'];
    });
}

[$results, $errors] = await_all($coroutines);

$elapsed = microtime(true) - $start_time;

echo "All queries completed\n";
echo "Error count: " . count($errors) . "\n";

sort($results);
echo "Results: " . implode(", ", $results) . "\n";

// If truly parallel: ~0.3s. If sequential: ~0.9s
if ($elapsed < 0.8) {
    echo "Execution was parallel\n";
} else {
    echo "WARNING: Execution appears sequential (took " . round($elapsed, 2) . "s)\n";
}

echo "OK\n";
?>
--EXPECT--
Testing parallel query execution timing...
All queries completed
Error count: 0
Results: 0, 1, 2
Execution was parallel
OK
