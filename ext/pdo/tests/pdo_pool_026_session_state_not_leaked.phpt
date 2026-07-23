--TEST--
PDO Pool: session state must not reach the next coroutine through a reused connection
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

// Two coroutines run on the same physical connection, one after the other, so
// everything the first one left in the session was visible to the second: user
// variables, sql_mode (which changes what identical SQL means), time_zone,
// temporary tables, and a held GET_LOCK only that connection could release.
// A slot that was touched this way is now cleaned before anyone else gets it.

$pdo = PDOPoolTest::poolFactory(1);   // one slot, so reuse is guaranteed

$connA = null;
await(spawn(function () use ($pdo, &$connA) {
    $connA = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
    $pdo->exec("SET @secret = 'coroutine-A-data'");
    $pdo->exec("SET SESSION sql_mode = 'ANSI_QUOTES'");
    $pdo->exec("CREATE TEMPORARY TABLE pdo_pool_026_tmp (v INT)");
    $pdo->exec("INSERT INTO pdo_pool_026_tmp VALUES (42)");
    echo "A took the lock: ", $pdo->query("SELECT GET_LOCK('pdo_pool_026', 0)")->fetchColumn(), "\n";
}));

await(spawn(function () use ($pdo, $connA) {
    // The connection itself is reused -- the slot is cleaned, not thrown away.
    echo "B on the same connection: ",
        var_export($pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $connA, true), "\n";

    echo "B sees @secret: ", var_export($pdo->query("SELECT @secret")->fetchColumn(), true), "\n";
    echo "B sees ANSI_QUOTES: ", var_export(
        str_contains($pdo->query("SELECT @@session.sql_mode")->fetchColumn(), 'ANSI_QUOTES'), true), "\n";
    echo "B holds the lock: ", var_export($pdo->query("SELECT IS_USED_LOCK('pdo_pool_026')")->fetchColumn(), true), "\n";

    try {
        $pdo->query("SELECT v FROM pdo_pool_026_tmp")->fetchColumn();
        echo "B sees the temporary table: true\n";
    } catch (PDOException $e) {
        echo "B sees the temporary table: false\n";
    }
}));

echo "Done\n";
?>
--EXPECT--
A took the lock: 1
B on the same connection: true
B sees @secret: NULL
B sees ANSI_QUOTES: false
B holds the lock: NULL
B sees the temporary table: false
Done
