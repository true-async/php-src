--TEST--
PDO Pool: Pool creation with ATTR_POOL_ENABLED
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

// Create PDO with pool enabled
$pdo = AsyncPDOMySQLTest::factory(options: [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 1,
    PDO::ATTR_POOL_MAX => 5,
]);

$pool = $pdo->getPool();

if ($pool === null) {
    echo "ERROR: getPool() returned null\n";
} else {
    echo "Pool created: " . get_class($pool) . "\n";
    echo "Pool is Async\\Pool: " . ($pool instanceof \Async\Pool ? "yes" : "no") . "\n";
}

echo "Done\n";
?>
--EXPECT--
Pool created: Async\Pool
Pool is Async\Pool: yes
Done
