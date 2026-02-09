--TEST--
PDO Pool: Pool cannot be combined with persistent connections
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

// Attempting persistent + pool should throw
try {
    $pdo = AsyncPDOMySQLTest::factory(options: [
        PDO::ATTR_PERSISTENT => true,
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_MIN => 1,
        PDO::ATTR_POOL_MAX => 5,
    ]);
    echo "FAIL: no exception thrown\n";
} catch (PDOException $e) {
    echo "Exception: " . $e->getMessage() . "\n";
}

echo "Done\n";
?>
--EXPECT--
Exception: PDO::ATTR_POOL_ENABLED cannot be used with PDO::ATTR_PERSISTENT
Done
