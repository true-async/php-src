--TEST--
PDO Pool: ATTR_POOL_INIT_SQL failing on replay, leaving a transaction, or cancelled midway
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

function show(Throwable $e): void {
    for ($n = 0; $e !== null; $e = $e->getPrevious(), $n++) {
        echo str_repeat('  ', $n), get_class($e), ': ', $e->getMessage(), "\n";
    }
}

$admin = PDOPoolTest::factory();
$admin->exec("DROP TABLE IF EXISTS pool_init_once");
$admin->exec("CREATE TABLE pool_init_once (id INT PRIMARY KEY)");

// Succeeds on the first connect, fails on every replay: the cleaned slot is
// not handed out, and the slot built in its place reports why.
$once = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_INIT_SQL => ["INSERT INTO pool_init_once VALUES (1)"],
]);
await(spawn(function () use ($once) {
    echo "first connect: ", $once->query("SELECT COUNT(*) FROM pool_init_once")->fetchColumn(), "\n";
    $once->exec("SET @dirty = 1");
}));
try {
    await(spawn(fn () => $once->query("SELECT 1")->fetchColumn()));
    echo "replay: no exception\n";
} catch (Throwable $e) {
    echo "replay:\n";
    show($e);
}
echo "slots: ", $once->getPool()->count(), "\n";

// A transaction left open would be inherited by every coroutine on the slot.
$txn = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_INIT_SQL => ["START TRANSACTION"],
]);
try {
    await(spawn(fn () => $txn->query("SELECT 1")->fetchColumn()));
    echo "open transaction: no exception\n";
} catch (Throwable $e) {
    echo "open transaction:\n";
    show($e);
}

// Cancelled while an init statement runs: the coroutine sees the
// cancellation, the half-configured slot is not kept, and the next coroutine
// gets a fully configured one.
$slow = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_INIT_SQL => ["DO SLEEP(0.5)", "SET @after = 'ran'"],
]);
$victim = spawn(fn () => $slow->query("SELECT 1")->fetchColumn());
delay(100);
$victim->cancel();
try {
    await($victim);
    echo "cancelled: no exception\n";
} catch (Throwable $e) {
    echo "cancelled: ", get_class($e), "\n";
}
echo "next: ", await(spawn(fn () => $slow->query("SELECT @after")->fetchColumn())), "\n";

$admin->exec("DROP TABLE pool_init_once");

echo "Done\n";
?>
--EXPECT--
first connect: 1
replay:
PDOException: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  PDOException: SQLSTATE[23000] [1062] Duplicate entry '1' for key 'PRIMARY'
slots: 0
open transaction:
PDOException: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  PDOException: SQLSTATE[HY000] [0] PDO::ATTR_POOL_INIT_SQL left a transaction open
cancelled: Async\AsyncCancellation
next: ran
Done
