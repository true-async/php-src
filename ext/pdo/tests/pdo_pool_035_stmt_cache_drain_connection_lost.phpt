--TEST--
PDO Pool: a connection lost while a dropped statement's result is read off is not handed out again
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
use function Async\delay;

// The connection is killed while a dropped statement's result is read off:
// in the middle of a large result set, or between result sets while the
// server runs SLEEP(). The read must end, and the pool must not hand out the
// dead connection.

$admin = PDOPoolTest::factory();
$admin->exec("DROP TABLE IF EXISTS pdo_pool_035");
$admin->exec("CREATE TABLE pdo_pool_035 (d INT)");
$admin->exec("INSERT INTO pdo_pool_035 VALUES (0),(1),(2),(3),(4),(5),(6),(7),(8),(9)");
$admin->exec("DROP PROCEDURE IF EXISTS pdo_pool_035_proc");
$admin->exec("CREATE PROCEDURE pdo_pool_035_proc() BEGIN SELECT 1; SELECT SLEEP(5); SELECT 'third'; END");

const LARGE = "SELECT a.d FROM pdo_pool_035 a, pdo_pool_035 b, pdo_pool_035 c, pdo_pool_035 e, pdo_pool_035 f, pdo_pool_035 g";

$cases = [
    // Far larger than the socket buffers: reading off meets the closed socket.
    "SELECT, killed mid-result" => [LARGE, false, false],
    "CALL, killed between result sets" => ["CALL pdo_pool_035_proc()", false, true],
    // Not a CALL and starts with DDL: nothing marks the session for a reset,
    // so only the failed read-off keeps the slot out of the pool.
    "multi-statement, emulated, killed between result sets" =>
        ["DROP TABLE IF EXISTS pdo_pool_035_none; SELECT SLEEP(5); SELECT 'third'", true, true],
];

foreach ($cases as $label => [$sql, $emulate, $between]) {
    echo "-- $label\n";
    $pdo = PDOPoolTest::poolFactory(1, [
        PDO::ATTR_POOL_STMT_CACHE_SIZE => 8,
        PDO::ATTR_EMULATE_PREPARES => $emulate,
        Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false,
    ]);

    $first = await(spawn(function () use ($pdo, $admin, $sql, $between) {
        $id = (int)$pdo->query("SELECT CONNECTION_ID()")->fetchAll(PDO::FETCH_COLUMN)[0];
        $stmt = $pdo->prepare($sql);
        $stmt->execute();
        if (!$between) {
            $stmt->fetch();
            $admin->exec("KILL CONNECTION $id");
            return $id; // the statement is dropped here, its rows unread
        }

        $stmt->fetchAll();
        // Runs while the read-off waits for the SLEEP() result set.
        $killer = spawn(function () use ($admin, $id) {
            delay(200);
            $admin->exec("KILL CONNECTION $id");
        });
        unset($stmt);
        await($killer);
        return $id;
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
-- SELECT, killed mid-result
next coroutine: another connection
-- CALL, killed between result sets
next coroutine: another connection
-- multi-statement, emulated, killed between result sets
next coroutine: another connection
Done
