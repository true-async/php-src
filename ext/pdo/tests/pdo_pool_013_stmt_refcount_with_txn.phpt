--TEST--
PDO Pool: Refcount and transaction pinning work together
--EXTENSIONS--
pdo
pdo_mysql
true_async
--SKIPIF--
<?php
require_once __DIR__ . '/inc/pdo_pool_test.inc';
PDOPoolTest::skip();
?>
--FILE--
<?php
require_once __DIR__ . '/inc/pdo_pool_test.inc';

use function Async\spawn;
use function Async\await;

$pdo = PDOPoolTest::poolFactory();
$pool = $pdo->getPool();

$pdo->exec("CREATE TABLE IF NOT EXISTS test_refcount_txn (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50)) ENGINE=InnoDB");
$pdo->exec("TRUNCATE TABLE test_refcount_txn");

$coro = spawn(function() use ($pdo, $pool) {
    $pdo->beginTransaction();

    $stmt1 = $pdo->prepare("INSERT INTO test_refcount_txn (val) VALUES (?)");
    $stmt1->execute(['row1']);

    $stmt2 = $pdo->prepare("INSERT INTO test_refcount_txn (val) VALUES (?)");
    $stmt2->execute(['row2']);

    // Destroy both statements — connection must stay held (in_txn)
    unset($stmt1);
    unset($stmt2);
    echo "After unset both stmts, idle: " . $pool->idleCount() . "\n";

    // Transaction still works
    $pdo->exec("INSERT INTO test_refcount_txn (val) VALUES ('row3')");
    $pdo->commit();
    echo "Committed\n";

    // After commit + no stmts, connection should be released
    echo "After commit, idle: " . $pool->idleCount() . "\n";

    return true;
});

await($coro);

// Verify all rows
$stmt = $pdo->query("SELECT val FROM test_refcount_txn ORDER BY id");
$rows = $stmt->fetchAll(PDO::FETCH_COLUMN, 0);
echo "Rows: " . implode(', ', $rows) . "\n";

$pdo->exec("DROP TABLE IF EXISTS test_refcount_txn");
echo "Done\n";
?>
--EXPECT--
After unset both stmts, idle: 0
Committed
After commit, idle: 1
Rows: row1, row2, row3
Done
