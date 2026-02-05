--TEST--
PDO Pool: Pool statistics via getPool()
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
    PDO::ATTR_POOL_MAX => 5,
]);

$pool = $pdo->getPool();

echo "Initial state:\n";
echo "  count: " . $pool->count() . "\n";
echo "  idleCount: " . $pool->idleCount() . "\n";
echo "  activeCount: " . $pool->activeCount() . "\n";
echo "  isClosed: " . ($pool->isClosed() ? "yes" : "no") . "\n";

// Run a query to create a connection
$coro = spawn(function() use ($pdo, $pool) {
    echo "In coroutine:\n";
    $stmt = $pdo->query("SELECT 1");
    echo "  count: " . $pool->count() . "\n";
    echo "  activeCount: " . $pool->activeCount() . "\n";
    unset($stmt);
    return "done";
});

await($coro);

echo "After coroutine:\n";
echo "  count: " . $pool->count() . "\n";
echo "  idleCount: " . $pool->idleCount() . "\n";
echo "  activeCount: " . $pool->activeCount() . "\n";

echo "Done\n";
?>
--EXPECT--
Initial state:
  count: 0
  idleCount: 0
  activeCount: 0
  isClosed: no
In coroutine:
  count: 1
  activeCount: 1
After coroutine:
  count: 1
  idleCount: 1
  activeCount: 0
Done
