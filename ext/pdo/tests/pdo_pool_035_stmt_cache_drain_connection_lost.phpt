--TEST--
PDO Pool: a connection lost while a cached statement's rows are read off is not handed out again
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

// The result is far larger than the socket buffers, so reading it off after
// the kill meets the closed connection. The read must end, and the pool must
// not hand out the dead connection.

$admin = PDOPoolTest::factory();
$admin->exec("DROP TABLE IF EXISTS pdo_pool_035");
$admin->exec("CREATE TABLE pdo_pool_035 (d INT)");
$admin->exec("INSERT INTO pdo_pool_035 VALUES (0),(1),(2),(3),(4),(5),(6),(7),(8),(9)");
$admin->exec("DROP PROCEDURE IF EXISTS pdo_pool_035_proc");
$admin->exec("CREATE PROCEDURE pdo_pool_035_proc() BEGIN
    SELECT a.d, b.d, c.d, e.d, f.d, g.d FROM pdo_pool_035 a, pdo_pool_035 b, pdo_pool_035 c,
        pdo_pool_035 e, pdo_pool_035 f, pdo_pool_035 g;
    SELECT 'second';
END");

foreach (["SELECT", "CALL"] as $kind) {
    echo "-- $kind\n";
    $pdo = PDOPoolTest::poolFactory(1, [
        PDO::ATTR_POOL_STMT_CACHE_SIZE => 8,
        PDO::ATTR_EMULATE_PREPARES => false,
        Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false,
    ]);
    $sql = $kind === "CALL" ? "CALL pdo_pool_035_proc()"
        : "SELECT a.d FROM pdo_pool_035 a, pdo_pool_035 b, pdo_pool_035 c, pdo_pool_035 e, pdo_pool_035 f, pdo_pool_035 g";

    $first = await(spawn(function () use ($pdo, $admin, $sql) {
        $id = (int)$pdo->query("SELECT CONNECTION_ID()")->fetchAll(PDO::FETCH_COLUMN)[0];
        $stmt = $pdo->prepare($sql);
        $stmt->execute();
        $stmt->fetch();
        $admin->exec("KILL CONNECTION $id");
        return $id; // the statement is dropped here, its rows unread
    }));

    echo "next coroutine: ", await(spawn(function () use ($pdo, $first) {
        try {
            $id = (int)$pdo->query("SELECT CONNECTION_ID()")->fetchAll(PDO::FETCH_COLUMN)[0];
            return $id === $first ? "same connection" : "another connection";
        } catch (PDOException $e) {
            return preg_replace('/^(SQLSTATE\[\w+\]: [^:]+: \d+).*$/s', '$1', $e->getMessage());
        }
    })), "\n";
}

unset($pdo);
$admin->exec("DROP PROCEDURE pdo_pool_035_proc");
$admin->exec("DROP TABLE pdo_pool_035");
echo "Done\n";
?>
--EXPECT--
-- SELECT
next coroutine: another connection
-- CALL
next coroutine: another connection
Done
