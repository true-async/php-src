--TEST--
PDO Pool: errorInfo returns native details when connection is held
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

$pdo = PDOPoolTest::poolFactory(extra: [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
]);

$coro = spawn(function() use ($pdo) {
    // query() holds connection via stmt refcount
    $stmt = $pdo->query("SELECT 1 FROM nonexistent_table_xyz");

    // Connection is still held — errorInfo should have native details
    $info = $pdo->errorInfo();
    echo "SQLSTATE: " . $info[0] . "\n";
    echo "Has native code: " . (isset($info[1]) && $info[1] !== null ? "yes" : "no") . "\n";
    echo "Has native message: " . (isset($info[2]) && $info[2] !== null ? "yes" : "no") . "\n";

    return true;
});

await($coro);
echo "Done\n";
?>
--EXPECT--
SQLSTATE: 42S02
Has native code: yes
Has native message: yes
Done
