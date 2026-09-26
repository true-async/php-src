--TEST--
PDO Pool: an error raised by exec() carries the driver's code and message
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

// exec() gives its connection back to the pool when it returns; the error must be
// read from that connection before it goes, as without the pool.

$pdo = PDOPoolTest::poolFactory();

await(spawn(function() use ($pdo) {
    try {
        $pdo->exec("SELECT 1 FROM nonexistent_table_xyz");
    } catch (PDOException $e) {
        echo "exception SQLSTATE: ", $e->errorInfo[0], "\n";
        echo "exception native code: ", $e->errorInfo[1] ?? 'none', "\n";
        echo "message has native code: ", str_contains($e->getMessage(), '1146') ? 'yes' : 'no', "\n";
    }

    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_WARNING);
    $pdo->exec("SELECT 1 FROM nonexistent_table_xyz");

    // The slot is back in the pool after the failure.
    echo "active after failures: ", $pdo->getPool()->activeCount(), "\n";
}));

echo "Done\n";
?>
--EXPECTF--
exception SQLSTATE: 42S02
exception native code: 1146
message has native code: yes

Warning: PDO::exec(): SQLSTATE[42S02]: Base table or view not found: 1146 %s in %s on line %d
active after failures: 0
Done
