--TEST--
PDO Pool: Destroying first statement does not break second statement
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

$pdo->exec("CREATE TABLE IF NOT EXISTS test_refcount2 (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_refcount2");

$coro = spawn(function() use ($pdo) {
    $stmt1 = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId1 = $stmt1->fetch()['cid'];

    $stmt2 = $pdo->prepare("INSERT INTO test_refcount2 (val) VALUES (?)");

    // Destroy first statement — connection must NOT be released
    unset($stmt1);

    // Second statement should still work on the same connection
    $stmt2->execute(['after_unset']);
    echo "Insert after unset(stmt1): ok\n";

    // Verify the connection is still the same
    $stmt3 = $pdo->query("SELECT CONNECTION_ID() as cid");
    $connId3 = $stmt3->fetch()['cid'];
    echo "Same connection after unset: " . ($connId1 === $connId3 ? "yes" : "no") . "\n";

    // Verify data was inserted
    $stmt4 = $pdo->query("SELECT val FROM test_refcount2");
    $row = $stmt4->fetch();
    echo "Value: {$row['val']}\n";

    return true;
});

await($coro);
$pdo->exec("DROP TABLE IF EXISTS test_refcount2");
echo "Done\n";
?>
--EXPECT--
Insert after unset(stmt1): ok
Same connection after unset: yes
Value: after_unset
Done
