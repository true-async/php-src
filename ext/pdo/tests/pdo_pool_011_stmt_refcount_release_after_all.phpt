--TEST--
PDO Pool: Connection released to pool only after all statements destroyed
--EXTENSIONS--
pdo
pdo_mysql
true_async
--SKIPIF--
<?php
$pdo_pool_inc_dir = getenv('REDIR_TEST_DIR');
if (false === $pdo_pool_inc_dir) $pdo_pool_inc_dir = __DIR__ . '/';
require_once $pdo_pool_inc_dir . 'inc/pdo_pool_test.inc';
PDOPoolTest::skip();
?>
--FILE--
<?php
$pdo_pool_inc_dir = getenv('REDIR_TEST_DIR');
if (false === $pdo_pool_inc_dir) $pdo_pool_inc_dir = __DIR__ . '/';
require_once $pdo_pool_inc_dir . 'inc/pdo_pool_test.inc';

use function Async\spawn;
use function Async\await;

$pdo = PDOPoolTest::poolFactory();
$pool = $pdo->getPool();

echo "Initial idle: " . $pool->idleCount() . "\n";

$coro = spawn(function() use ($pdo, $pool) {
    $stmt1 = $pdo->query("SELECT 1");
    $stmt2 = $pdo->query("SELECT 2");

    echo "With 2 stmts, idle: " . $pool->idleCount() . "\n";

    // Destroy first — connection should stay held (refcount > 0)
    unset($stmt1);
    echo "After unset(stmt1), idle: " . $pool->idleCount() . "\n";

    // Destroy second — refcount reaches 0, connection released
    unset($stmt2);
    echo "After unset(stmt2), idle: " . $pool->idleCount() . "\n";

    return true;
});

await($coro);
echo "After coroutine, idle: " . $pool->idleCount() . "\n";
echo "Done\n";
?>
--EXPECT--
Initial idle: 0
With 2 stmts, idle: 0
After unset(stmt1), idle: 0
After unset(stmt2), idle: 1
After coroutine, idle: 1
Done
