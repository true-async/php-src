--TEST--
PDO Pool: lastInsertId works when connection held by statement
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

$pdo->exec("CREATE TABLE IF NOT EXISTS test_lastid (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_lastid");

$coro = spawn(function() use ($pdo) {
    // prepare keeps connection in slot via refcount
    $stmt = $pdo->prepare("INSERT INTO test_lastid (val) VALUES (?)");
    $stmt->execute(['first']);

    // Connection is still held by $stmt — lastInsertId should work
    $id1 = $pdo->lastInsertId();
    echo "First ID: " . ($id1 !== false && $id1 > 0 ? "valid" : "invalid") . "\n";

    $stmt->execute(['second']);
    $id2 = $pdo->lastInsertId();
    echo "Second ID: " . ($id2 !== false && $id2 > $id1 ? "valid" : "invalid") . "\n";

    return true;
});

await($coro);
$pdo->exec("DROP TABLE IF EXISTS test_lastid");
echo "Done\n";
?>
--EXPECT--
First ID: valid
Second ID: valid
Done
