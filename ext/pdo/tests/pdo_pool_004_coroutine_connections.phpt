--TEST--
PDO Pool: Each coroutine gets its own connection from pool
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

$pool = $pdo->getPool();
echo "Pool created\n";
echo "Initial pool count: " . $pool->count() . "\n";

// Run queries in coroutines
$coro1 = spawn(function() use ($pdo) {
    $stmt = $pdo->query("SELECT CONNECTION_ID() as id");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return (int)$row['id'];
});

$coro2 = spawn(function() use ($pdo) {
    $stmt = $pdo->query("SELECT CONNECTION_ID() as id");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    return (int)$row['id'];
});

$connId1 = await($coro1);
$connId2 = await($coro2);

echo "Connection 1 ID: " . ($connId1 > 0 ? "valid" : "invalid") . "\n";
echo "Connection 2 ID: " . ($connId2 > 0 ? "valid" : "invalid") . "\n";
echo "Connections are different: " . ($connId1 !== $connId2 ? "yes" : "no") . "\n";
echo "Pool count after coroutines: " . $pool->count() . "\n";

echo "Done\n";
?>
--EXPECT--
Pool created
Initial pool count: 0
Connection 1 ID: valid
Connection 2 ID: valid
Connections are different: yes
Pool count after coroutines: 2
Done
