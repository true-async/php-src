--TEST--
PostgreSQL concurrent query - basic usage
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
$table_name = "table_concurrent_001";

// Create test table
pg_query($db, "DROP TABLE IF EXISTS {$table_name}");
pg_query($db, "CREATE TABLE {$table_name} (id int, value text)");
pg_query($db, "INSERT INTO {$table_name} VALUES (1, 'test1'), (2, 'test2'), (3, 'test3')");

echo "Testing concurrent pg_query()...\n";

// Test 1: Simple query in coroutine
$coroutine = spawn(function() use ($db, $table_name) {
    echo "Coroutine: executing query\n";
    $result = pg_query($db, "SELECT * FROM {$table_name} WHERE id = 1");

    if (!$result) {
        echo "ERROR: Query failed\n";
        return;
    }

    $row = pg_fetch_assoc($result);
    echo "Coroutine: got result id={$row['id']}, value={$row['value']}\n";

    pg_free_result($result);
});

$result = await($coroutine);

echo "Main: coroutine completed\n";

// Cleanup
pg_query($db, "DROP TABLE {$table_name}");
pg_close($db);

echo "OK\n";
?>
--EXPECT--
Testing concurrent pg_query()...
Coroutine: executing query
Coroutine: got result id=1, value=test1
Main: coroutine completed
OK
