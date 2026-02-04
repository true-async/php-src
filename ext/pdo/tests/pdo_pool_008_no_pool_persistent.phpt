--TEST--
PDO Pool: Pool is not created for persistent connections
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

// Create persistent PDO with pool enabled
$pdo = AsyncPDOMySQLTest::factory(options: [
    PDO::ATTR_PERSISTENT => true,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 1,
    PDO::ATTR_POOL_MAX => 5,
]);

$pool = $pdo->getPool();

if ($pool === null) {
    echo "Pool not created for persistent connection: OK\n";
} else {
    echo "Pool should not be created for persistent connection: FAIL\n";
}

echo "Done\n";
?>
--EXPECT--
Pool not created for persistent connection: OK
Done
