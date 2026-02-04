--TEST--
PDO Pool: getPool() returns null when pool is not enabled
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

// Create PDO without pool enabled
$pdo = AsyncPDOMySQLTest::factory();

$pool = $pdo->getPool();

if ($pool === null) {
    echo "getPool() returns null when pool disabled: OK\n";
} else {
    echo "getPool() should return null but got: " . get_class($pool) . "\n";
}

echo "Done\n";
?>
--EXPECT--
getPool() returns null when pool disabled: OK
Done
