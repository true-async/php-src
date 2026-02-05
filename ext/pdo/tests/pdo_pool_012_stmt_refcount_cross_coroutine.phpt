--TEST--
PDO Pool: Different coroutines get different connections, each with own refcount
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

$pdo->exec("CREATE TABLE IF NOT EXISTS test_refcount3 (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_refcount3");

// Each coroutine creates two statements on its own connection
$coro1 = spawn(function() use ($pdo) {
    $stmtA = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId = $stmtA->fetch()['cid'];

    $stmtB = $pdo->prepare("INSERT INTO test_refcount3 (val) VALUES (?)");

    // Destroy stmtA — stmtB still holds the connection
    unset($stmtA);

    $stmtB->execute(['coro1']);

    // Confirm still on same connection
    $stmtC = $pdo->query("SELECT CONNECTION_ID() as cid");
    $check = $stmtC->fetch()['cid'];
    echo "Coro1 same conn: " . ($connId === $check ? "yes" : "no") . "\n";

    return $connId;
});

$coro2 = spawn(function() use ($pdo) {
    $stmtA = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId = $stmtA->fetch()['cid'];

    $stmtB = $pdo->prepare("INSERT INTO test_refcount3 (val) VALUES (?)");

    unset($stmtA);

    $stmtB->execute(['coro2']);

    $stmtC = $pdo->query("SELECT CONNECTION_ID() as cid");
    $check = $stmtC->fetch()['cid'];
    echo "Coro2 same conn: " . ($connId === $check ? "yes" : "no") . "\n";

    return $connId;
});

$connId1 = await($coro1);
$connId2 = await($coro2);

echo "Different connections across coroutines: " . ($connId1 !== $connId2 ? "yes" : "no") . "\n";

// Verify data
$stmt = $pdo->query("SELECT val FROM test_refcount3 ORDER BY id");
$rows = $stmt->fetchAll(PDO::FETCH_COLUMN, 0);
sort($rows);
echo "Rows: " . implode(', ', $rows) . "\n";

$pdo->exec("DROP TABLE IF EXISTS test_refcount3");
echo "Done\n";
?>
--EXPECT--
Coro1 same conn: yes
Coro2 same conn: yes
Different connections across coroutines: yes
Rows: coro1, coro2
Done
