--TEST--
PDO Pool: ATTR_POOL_INIT_SQL is replayed after a slot is cleaned
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

// The reset that cleans a dirty slot returns the session to the server's
// global defaults; the init SQL is the slot's baseline, so it must run again
// on the same physical connection, after the driver's own init commands.

$pdo = PDOPoolTest::poolFactory(1, [   // one slot, so reuse is guaranteed
    PDO::ATTR_POOL_INIT_SQL => ["SET time_zone = '+05:00'", "SET @baseline = 'init'"],
]);

$conn = null;
await(spawn(function () use ($pdo, &$conn) {
    $conn = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
    echo "fresh: ", $pdo->query("SELECT @@session.time_zone")->fetchColumn(),
         " ", $pdo->query("SELECT @baseline")->fetchColumn(), "\n";

    // This coroutine's own change dirties the slot.
    $pdo->exec("SET time_zone = '+01:00'");
    $pdo->exec("SET @baseline = 'coroutine'");
    echo "changed: ", $pdo->query("SELECT @@session.time_zone")->fetchColumn(), "\n";
}));

await(spawn(function () use ($pdo, $conn) {
    echo "same connection: ", var_export(
        $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $conn, true), "\n";
    echo "after reset: ", $pdo->query("SELECT @@session.time_zone")->fetchColumn(),
         " ", $pdo->query("SELECT @baseline")->fetchColumn(), "\n";
}));

// A failing statement fails the connect with the server's error.
$bad = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_INIT_SQL => ["SET time_zone = 'Nowhere/Never'"],
]);
try {
    await(spawn(fn () => $bad->query("SELECT 1")));
    echo "bad: no exception\n";
} catch (PDOException $e) {
    echo "bad: ", $e->getMessage(), "\n";
    echo "  ", $e->getPrevious()?->getMessage(), "\n";
}

echo "Done\n";
?>
--EXPECT--
fresh: +05:00 init
changed: +01:00
same connection: true
after reset: +05:00 init
bad: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  SQLSTATE[HY000] [1298] Unknown or incorrect time zone: 'Nowhere/Never'
Done
