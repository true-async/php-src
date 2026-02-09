--TEST--
PDO Pool: Two statements in same coroutine share same connection
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

$pdo->exec("CREATE TABLE IF NOT EXISTS test_refcount (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_refcount");

$coro = spawn(function() use ($pdo) {
    // Two statements should get the same underlying connection
    $stmt1 = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId1 = $stmt1->fetch()['cid'];

    $stmt2 = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId2 = $stmt2->fetch()['cid'];

    echo "Same connection: " . ($connId1 === $connId2 ? "yes" : "no") . "\n";

    // Both statements should still be functional
    $stmt3 = $pdo->prepare("INSERT INTO test_refcount (val) VALUES (?)");
    $stmt3->execute(['hello']);

    $stmt4 = $pdo->prepare("SELECT val FROM test_refcount WHERE id = ?");
    $stmt4->execute([1]);
    $row = $stmt4->fetch();
    echo "Value: {$row['val']}\n";

    return true;
});

await($coro);
$pdo->exec("DROP TABLE IF EXISTS test_refcount");
echo "Done\n";
?>
--EXPECT--
Same connection: yes
Value: hello
Done
