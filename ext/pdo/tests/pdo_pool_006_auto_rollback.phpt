--TEST--
PDO Pool: Uncommitted transaction is rolled back when connection returns to pool
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
    PDO::ATTR_POOL_MAX => 2,
]);

// Initialize database
$pdo->exec("CREATE TABLE IF NOT EXISTS test_pool_auto_rb (id INT PRIMARY KEY, value VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_pool_auto_rb");
$pdo->exec("INSERT INTO test_pool_auto_rb VALUES (1, 'initial')");

echo "Testing auto-rollback on coroutine end\n";

// Coroutine that starts transaction but doesn't commit
$coro = spawn(function() use ($pdo) {
    echo "Coro: starting transaction\n";
    $pdo->beginTransaction();
    $pdo->exec("UPDATE test_pool_auto_rb SET value = 'modified' WHERE id = 1");
    echo "Coro: updated but NOT committing\n";
    // Transaction not committed - should be rolled back when coroutine ends
    return "coro_done";
});

await($coro);
echo "Coroutine finished\n";

// Check that the change was rolled back
$stmt = $pdo->query("SELECT value FROM test_pool_auto_rb WHERE id = 1");
$row = $stmt->fetch(PDO::FETCH_ASSOC);

echo "Value after coroutine: " . $row['value'] . "\n";
if ($row['value'] === 'initial') {
    echo "Auto-rollback: OK\n";
} else {
    echo "Auto-rollback: FAILED (expected 'initial')\n";
}

// Cleanup
$pdo->exec("DROP TABLE IF EXISTS test_pool_auto_rb");

echo "Done\n";
?>
--EXPECT--
Testing auto-rollback on coroutine end
Coro: starting transaction
Coro: updated but NOT committing
Coroutine finished
Value after coroutine: initial
Auto-rollback: OK
Done
