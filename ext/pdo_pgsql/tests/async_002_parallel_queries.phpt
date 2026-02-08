--TEST--
PDO PgSQL concurrent query - parallel execution
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
$table_name = "test_pdo_async_002";

// Create test table
$db->exec("DROP TABLE IF EXISTS {$table_name}");
$db->exec("CREATE TABLE {$table_name} (id int, value text)");
$db->exec("INSERT INTO {$table_name} VALUES (1, 'alpha'), (2, 'beta'), (3, 'gamma')");

echo "Testing parallel concurrent queries...\n";

// Launch 3 queries in parallel
$coroutines = [];

$coroutines[] = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $stmt = $db->query("SELECT * FROM {$table_name} WHERE id = 1");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row['value'];
});

$coroutines[] = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $stmt = $db->query("SELECT * FROM {$table_name} WHERE id = 2");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row['value'];
});

$coroutines[] = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $stmt = $db->query("SELECT * FROM {$table_name} WHERE id = 3");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return $row['value'];
});

// Wait for all to complete
[$results, $errors] = await_all($coroutines);

echo "All queries completed\n";
echo "Error count: " . count($errors) . "\n";
sort($results);
echo "Results: " . implode(", ", $results) . "\n";

// Cleanup
$db->exec("DROP TABLE {$table_name}");

echo "OK\n";
?>
--EXPECT--
Testing parallel concurrent queries...
All queries completed
Error count: 0
Results: alpha, beta, gamma
OK
