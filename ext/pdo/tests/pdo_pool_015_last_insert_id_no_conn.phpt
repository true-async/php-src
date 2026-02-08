--TEST--
PDO Pool: lastInsertId returns false when connection already released
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

$pdo->exec("CREATE TABLE IF NOT EXISTS test_lastid2 (id INT PRIMARY KEY AUTO_INCREMENT, val VARCHAR(50))");
$pdo->exec("TRUNCATE TABLE test_lastid2");

$coro = spawn(function() use ($pdo) {
    // exec releases the connection immediately after
    $pdo->exec("INSERT INTO test_lastid2 (val) VALUES ('hello')");

    // Connection already returned to pool — lastInsertId cannot get correct value
    $id = $pdo->lastInsertId();
    echo "lastInsertId after exec: " . var_export($id, true) . "\n";

    return true;
});

await($coro);
$pdo->exec("DROP TABLE IF EXISTS test_lastid2");
echo "Done\n";
?>
--EXPECT--
lastInsertId after exec: false
Done
