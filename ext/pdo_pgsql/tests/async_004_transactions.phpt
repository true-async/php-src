--TEST--
PDO PgSQL concurrent query - transactions
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
$table_name = "test_pdo_async_004";

// Create test table
$db->exec("DROP TABLE IF EXISTS {$table_name}");
$db->exec("CREATE TABLE {$table_name} (id int PRIMARY KEY, value text)");

echo "Testing transactions in coroutines\n";

// Coroutine 1: Insert and commit
$coro1 = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    echo "Coro1: starting transaction\n";
    $db->beginTransaction();
    echo "Coro1: inTransaction = " . ($db->inTransaction() ? "yes" : "no") . "\n";
    $db->exec("INSERT INTO {$table_name} VALUES (1, 'committed')");
    $db->commit();
    echo "Coro1: committed\n";
});

await($coro1);

// Coroutine 2: Insert and rollback
$coro2 = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    echo "Coro2: starting transaction\n";
    $db->beginTransaction();
    echo "Coro2: inTransaction = " . ($db->inTransaction() ? "yes" : "no") . "\n";
    $db->exec("INSERT INTO {$table_name} VALUES (2, 'rolled_back')");
    $db->rollBack();
    echo "Coro2: rolled back\n";
});

await($coro2);

// Check results
$stmt = $db->query("SELECT * FROM {$table_name} ORDER BY id");
$rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

echo "Rows in table: " . count($rows) . "\n";
foreach ($rows as $row) {
    echo "  id={$row['id']}, value={$row['value']}\n";
}

// Cleanup
$db->exec("DROP TABLE {$table_name}");

echo "Done\n";
?>
--EXPECT--
Testing transactions in coroutines
Coro1: starting transaction
Coro1: inTransaction = yes
Coro1: committed
Coro2: starting transaction
Coro2: inTransaction = yes
Coro2: rolled back
Rows in table: 1
  id=1, value=committed
Done
