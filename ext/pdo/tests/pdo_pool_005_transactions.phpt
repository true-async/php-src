--TEST--
PDO Pool: Transaction state tracked per pooled connection
--EXTENSIONS--
pdo
pdo_mysql
true_async
--SKIPIF--
<?php
require_once __DIR__ . '/../../async/tests/pdo_mysql/inc/async_pdo_mysql_test.inc';
AsyncPDOMySQLTest::skip();
?>
--FILE--
<?php
require_once __DIR__ . '/../../async/tests/pdo_mysql/inc/async_pdo_mysql_test.inc';

use function Async\spawn;
use function Async\await;

// Create PDO with pool enabled
$pdo = AsyncPDOMySQLTest::factory(options: [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 3,
]);

// Initialize database
$pdo->exec("CREATE TABLE IF NOT EXISTS test_pool_txn (id INT PRIMARY KEY, value VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_pool_txn");

echo "Testing transactions in coroutines\n";

// Coroutine 1: Start transaction, insert, commit
$coro1 = spawn(function() use ($pdo) {
    echo "Coro1: starting transaction\n";
    $pdo->beginTransaction();
    echo "Coro1: inTransaction = " . ($pdo->inTransaction() ? "yes" : "no") . "\n";
    $pdo->exec("INSERT INTO test_pool_txn VALUES (1, 'from_coro1')");
    $pdo->commit();
    echo "Coro1: committed\n";
    return "coro1_done";
});

// Await coro1 first to get deterministic output
await($coro1);

// Coroutine 2: Start transaction, insert, rollback
$coro2 = spawn(function() use ($pdo) {
    echo "Coro2: starting transaction\n";
    $pdo->beginTransaction();
    echo "Coro2: inTransaction = " . ($pdo->inTransaction() ? "yes" : "no") . "\n";
    $pdo->exec("INSERT INTO test_pool_txn VALUES (2, 'from_coro2')");
    $pdo->rollBack();
    echo "Coro2: rolled back\n";
    return "coro2_done";
});

await($coro2);

// Check results
$stmt = $pdo->query("SELECT * FROM test_pool_txn ORDER BY id");
$rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

echo "Rows in table: " . count($rows) . "\n";
foreach ($rows as $row) {
    echo "  id={$row['id']}, value={$row['value']}\n";
}

// Cleanup
$pdo->exec("DROP TABLE IF EXISTS test_pool_txn");

echo "Done\n";
?>
--EXPECTF--
Testing transactions in coroutines
Coro1: starting transaction
Coro1: inTransaction = yes
Coro1: committed
Coro2: starting transaction
Coro2: inTransaction = yes
Coro2: rolled back
Rows in table: 1
  id=1, value=from_coro1
Done
