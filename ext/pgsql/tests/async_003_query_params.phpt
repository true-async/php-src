--TEST--
PostgreSQL concurrent query - pg_query_params() with parameters
--EXTENSIONS--
pgsql
true_async
--SKIPIF--
<?php include("inc/skipif.inc"); ?>
--FILE--
<?php

use function Async\spawn;
use function Async\await;

include('inc/config.inc');

$db = pg_connect($conn_str);
$table_name = "table_concurrent_003";

// Create test table
pg_query($db, "DROP TABLE IF EXISTS {$table_name}");
pg_query($db, "CREATE TABLE {$table_name} (id int, name text, score int)");
pg_query($db, "INSERT INTO {$table_name} VALUES (1, 'Alice', 95), (2, 'Bob', 87), (3, 'Charlie', 92)");

echo "Testing concurrent pg_query_params()...\n";

// Test parameterized query in coroutine
$coroutine = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    echo "Coroutine: executing parameterized query\n";

    $result = pg_query_params($db,
        "SELECT * FROM {$table_name} WHERE name = $1 AND score > $2",
        ['Bob', 80]
    );

    if (!$result) {
        echo "ERROR: Query failed\n";
        pg_close($db);
        return;
    }

    $row = pg_fetch_assoc($result);
    if ($row) {
        echo "Coroutine: got result id={$row['id']}, name={$row['name']}, score={$row['score']}\n";
    } else {
        echo "ERROR: No results\n";
    }

    pg_free_result($result);
    pg_close($db);
});

await($coroutine);

echo "Main: coroutine completed\n";

// Test with multiple concurrent parameterized queries
echo "Testing multiple concurrent parameterized queries...\n";

$coro1 = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    $result = pg_query_params($db, "SELECT * FROM {$table_name} WHERE id = $1", [1]);
    $row = pg_fetch_assoc($result);
    echo "Coro1: {$row['name']}\n";
    pg_free_result($result);
    pg_close($db);
});

$coro2 = spawn(function() use ($conn_str, $table_name) {
    $db = pg_connect($conn_str);
    $result = pg_query_params($db, "SELECT * FROM {$table_name} WHERE id = $1", [3]);
    $row = pg_fetch_assoc($result);
    echo "Coro2: {$row['name']}\n";
    pg_free_result($result);
    pg_close($db);
});

await($coro1);
await($coro2);

// Cleanup
pg_query($db, "DROP TABLE {$table_name}");
pg_close($db);

echo "OK\n";
?>
--EXPECT--
Testing concurrent pg_query_params()...
Coroutine: executing parameterized query
Coroutine: got result id=2, name=Bob, score=87
Main: coroutine completed
Testing multiple concurrent parameterized queries...
Coro1: Alice
Coro2: Charlie
OK
