--TEST--
PDO Pool: a transaction opened with raw SQL pins its slot and is not leaked
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

// The pool tracked transactions through PDO's own in_txn flag, which only
// beginTransaction() sets. A transaction opened with raw SQL was therefore
// invisible: its slot went back to the pool mid-transaction and the next
// coroutine inherited the open transaction, its dirty rows and its row locks.

$pdo = PDOPoolTest::poolFactory(2);
$pdo->exec("DROP TABLE IF EXISTS pdo_pool_025");
$pdo->exec("CREATE TABLE pdo_pool_025 (v INT) ENGINE=InnoDB");

$openTrx = fn(PDO $p): int => (int) $p->query(
    "SELECT COUNT(*) FROM information_schema.innodb_trx"
    . " WHERE trx_mysql_thread_id = CONNECTION_ID()")->fetchColumn();

await(spawn(function () use ($pdo, $openTrx) {
    $pdo->exec("START TRANSACTION");
    $pdo->exec("INSERT INTO pdo_pool_025 VALUES (1)");
    // The slot must still be ours several statements later.
    echo "A: own open transaction: ", $openTrx($pdo), "\n";
}));

await(spawn(function () use ($pdo, $openTrx) {
    echo "B: inherited transaction: ", $openTrx($pdo), "\n";
    echo "B: sees uncommitted rows: ", (int) $pdo->query("SELECT COUNT(*) FROM pdo_pool_025")->fetchColumn(), "\n";
}));

$pdo->exec("DROP TABLE pdo_pool_025");

echo "Done\n";
?>
--EXPECT--
A: own open transaction: 1
B: inherited transaction: 0
B: sees uncommitted rows: 0
Done
