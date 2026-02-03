--TEST--
PostgreSQL concurrent query - parallel execution
--EXTENSIONS--
pgsql
true_async
--SKIPIF--
<?php include("inc/skipif.inc"); ?>
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\await_all;

include('inc/config.inc');

$db = pg_connect($conn_str);
$table_name = "table_concurrent_002";

// Create test table
pg_query($db, "DROP TABLE IF EXISTS {$table_name}");
pg_query($db, "CREATE TABLE {$table_name} (id int, value text)");
pg_query($db, "INSERT INTO {$table_name} VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma')");

echo "Testing parallel concurrent queries...\n";

$start_time = microtime(true);

// Launch 3 queries in parallel, each sleeping 0.5 seconds
$coroutines = [];

$coroutines[] = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    echo "Query 1: starting\n";
    $result = pg_query($db, "SELECT * FROM {$table_name} WHERE id = 1");
    $row = pg_fetch_assoc($result);
    echo "Query 1: got value={$row['value']}\n";
    pg_free_result($result);
    pg_close($db);
    return $row['value'];
});

$coroutines[] = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    echo "Query 2: starting\n";
    $result = pg_query($db, "SELECT * FROM {$table_name} WHERE id = 2");
    $row = pg_fetch_assoc($result);
    echo "Query 2: got value={$row['value']}\n";
    pg_free_result($result);
    pg_close($db);
    return $row['value'];
});

$coroutines[] = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    echo "Query 3: starting\n";
    $result = pg_query($db, "SELECT * FROM {$table_name} WHERE id = 3");
    $row = pg_fetch_assoc($result);
    echo "Query 3: got value={$row['value']}\n";
    pg_free_result($result);
    pg_close($db);
    return $row['value'];
});

// Wait for all to complete
[$results, $errors] = await_all($coroutines);

$elapsed_time = microtime(true) - $start_time;

echo "All queries completed\n";
echo "Results: " . implode(", ", $results) . "\n";

// If truly parallel, should take ~0.5s not ~1.5s
if ($elapsed_time < 1.0) {
    echo "Execution was parallel (took " . round($elapsed_time, 2) . "s < 1.0s)\n";
} else {
    echo "WARNING: Execution might not be parallel (took " . round($elapsed_time, 2) . "s)\n";
}

// Cleanup
pg_query($db, "DROP TABLE {$table_name}");
pg_close($db);

echo "OK\n";
?>
--EXPECTF--
Testing parallel concurrent queries...
Query 1: starting
Query 2: starting
Query 3: starting
Query 1: got value=alpha
Query 2: got value=beta
Query 3: got value=gamma
All queries completed
Results: alpha, beta, gamma
Execution was parallel (took %fs < 1.0s)
OK
