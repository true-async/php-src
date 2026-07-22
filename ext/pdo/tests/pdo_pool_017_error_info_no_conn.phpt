--TEST--
PDO Pool: errorInfo returns SQLSTATE but no native details when connection released
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

$pdo = PDOPoolTest::poolFactory(extra: [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
]);

$coro = spawn(function() use ($pdo) {
    // exec releases connection immediately
    $pdo->exec("SELECT 1 FROM nonexistent_table_xyz");

    // Connection released — SQLSTATE is synced but native details unavailable
    $info = $pdo->errorInfo();
    echo "SQLSTATE: " . $info[0] . "\n";
    echo "Native code: " . var_export($info[1], true) . "\n";
    echo "Native message: " . var_export($info[2], true) . "\n";

    // errorCode still works (synced to template)
    echo "errorCode: " . $pdo->errorCode() . "\n";

    return true;
});

await($coro);
echo "Done\n";
?>
--EXPECT--
SQLSTATE: 42S02
Native code: NULL
Native message: NULL
errorCode: 42S02
Done
