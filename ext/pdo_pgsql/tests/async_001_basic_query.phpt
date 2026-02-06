--TEST--
PDO PgSQL concurrent query - basic usage
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
use function Async\await;

require_once __DIR__ . "/config.inc";

$dsn = $config['ENV']['PDOTEST_DSN'];
$db = new PDO($dsn);
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$table_name = "test_pdo_async_001";

// Create test table
$db->exec("DROP TABLE IF EXISTS {$table_name}");
$db->exec("CREATE TABLE {$table_name} (id int, value text)");
$db->exec("INSERT INTO {$table_name} VALUES (1, 'test1'), (2, 'test2'), (3, 'test3')");

echo "Testing concurrent PDO::query()...\n";

// Test 1: Simple query in coroutine
$coroutine = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    echo "Coroutine: executing query\n";
    $stmt = $db->query("SELECT * FROM {$table_name} WHERE id = 1");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    echo "Coroutine: got result id={$row['id']}, value={$row['value']}\n";
});

await($coroutine);

echo "Main: coroutine completed\n";

// Cleanup
$db->exec("DROP TABLE {$table_name}");

echo "OK\n";
?>
--EXPECT--
Testing concurrent PDO::query()...
Coroutine: executing query
Coroutine: got result id=1, value=test1
Main: coroutine completed
OK
