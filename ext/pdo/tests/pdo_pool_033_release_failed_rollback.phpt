--TEST--
PDO Pool: a slot whose release-time rollback fails, or leaves a transaction open, is not reused
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

// A coroutine that ends inside a transaction gets it rolled back when its
// slot returns to the pool. When that rollback fails (the connection is dead,
// or a live one refuses it) or a transaction is still open after it
// (completion_type = CHAIN), the slot must be destroyed: the next coroutine
// would inherit a dead connection or the open transaction and its locks.

$admin = PDOPoolTest::factory();
$admin->exec("SET SESSION innodb_lock_wait_timeout = 1");
$admin->exec("DROP TABLE IF EXISTS pdo_pool_033");
$admin->exec("CREATE TABLE pdo_pool_033 (id INT PRIMARY KEY, v INT) ENGINE=InnoDB");
$admin->exec("INSERT INTO pdo_pool_033 VALUES (1, 0), (2, 0)");

function short_error(Throwable $e): string {
    return get_class($e) . " " . preg_replace('/^(SQLSTATE\[\w+\]: [^:]+: \d+).*$/s', '$1', $e->getMessage());
}

// Reads the whole result: an unbuffered statement that goes to the statement
// cache with rows left would block the connection.
function connection_id(PDO $pdo): int {
    return (int)$pdo->query("SELECT CONNECTION_ID()")->fetchAll(PDO::FETCH_COLUMN)[0];
}

// Opens a transaction in its own coroutine, locks row 1 and runs $then on the
// way out. Returns the connection id the coroutine used.
function first_coroutine(PDO $pdo, string $begin, ?Closure $then = null): int {
    return await(spawn(function () use ($pdo, $begin, $then) {
        if ($begin === 'beginTransaction') {
            $pdo->beginTransaction();
        } else {
            $pdo->exec($begin);
        }
        $id = connection_id($pdo);
        $pdo->exec("UPDATE pdo_pool_033 SET v = v + 1 WHERE id = 1");
        if ($then !== null) {
            $then($pdo, $id);
        }
        return $id;
    }));
}

// What the next coroutine and another connection see after the first one ended.
function report(PDO $admin, PDO $pdo, int $first_id): void {
    try {
        $admin->exec("UPDATE pdo_pool_033 SET v = 0 WHERE id = 1");
        echo "  row 1 is free\n";
    } catch (PDOException $e) {
        echo "  row 1: ", short_error($e), "\n";
    }

    echo "  next coroutine: ", await(spawn(function () use ($pdo, $first_id) {
        try {
            $id = connection_id($pdo);
        } catch (PDOException $e) {
            return short_error($e);
        }
        return ($id === $first_id ? "same connection" : "another connection")
            . ", in transaction: " . var_export($pdo->inTransaction(), true);
    })), "\n";
    echo "  pool count: ", $pdo->getPool()->count(), "\n";
}

function kill(PDO $admin, int $id): void {
    $admin->exec("KILL CONNECTION " . $id);
}

echo "-- clean transactional release keeps the slot\n";
$pdo = PDOPoolTest::poolFactory(1);
report($admin, $pdo, first_coroutine($pdo, 'beginTransaction'));

echo "-- the rollback fails on a live connection: an unread cursor\n";
// The dropped statement goes to the statement cache with its result unread;
// MySQL refuses the release-time rollback with 2014.
$pdo = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_STMT_CACHE_SIZE => 8,
    PDO::ATTR_EMULATE_PREPARES => false,
    Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false,
]);
report($admin, $pdo, first_coroutine($pdo, 'beginTransaction', function (PDO $pdo) {
    $stmt = $pdo->prepare("SELECT id FROM pdo_pool_033 ORDER BY id");
    $stmt->execute();
    $stmt->fetchColumn();
}));

foreach (['beginTransaction', 'START TRANSACTION'] as $begin) {
    echo "-- the connection is killed inside a transaction ($begin)\n";
    $pdo = PDOPoolTest::poolFactory(1);
    report($admin, $pdo, first_coroutine($pdo, $begin, fn (PDO $pdo, int $id) => kill($admin, $id)));
}

echo "-- the connection is killed while a statement outlives the coroutine\n";
$pdo = PDOPoolTest::poolFactory(1);
$held = new stdClass(); // an object: the closure must not keep the statement in its scope
$first_id = first_coroutine($pdo, 'beginTransaction', function (PDO $pdo, int $id) use ($admin, $held) {
    $held->stmt = $pdo->prepare("SELECT id FROM pdo_pool_033 ORDER BY id");
    $held->stmt->execute();
    kill($admin, $id);
});
unset($held->stmt);
report($admin, $pdo, $first_id);

echo "-- a coroutine queued for the only slot while its holder's connection is killed\n";
$pdo = PDOPoolTest::poolFactory(1);
$holder = spawn(function () use ($pdo, $admin) {
    $pdo->beginTransaction();
    $id = connection_id($pdo);
    delay(200);
    kill($admin, $id);
    return $id;
});
delay(50);
$waiter = spawn(function () use ($pdo) {
    try {
        return connection_id($pdo);
    } catch (PDOException $e) {
        return short_error($e);
    }
});
$first_id = await($holder);
$waiter_id = await($waiter);
echo "  waiter: ", is_int($waiter_id) ? ($waiter_id === $first_id ? "same connection" : "another connection") : $waiter_id, "\n";

echo "-- XA transaction: ROLLBACK is refused\n";
$pdo = PDOPoolTest::poolFactory(1);
report($admin, $pdo, first_coroutine($pdo, "XA START 'pdo_pool_033'"));

echo "-- completion_type = CHAIN: the rollback opens the next transaction\n";
$pdo = PDOPoolTest::poolFactory(1, [PDO::ATTR_POOL_INIT_SQL => ["SET SESSION completion_type = CHAIN"]]);
report($admin, $pdo, first_coroutine($pdo, 'beginTransaction'));

unset($pdo);
$admin->exec("DROP TABLE pdo_pool_033");
echo "Done\n";
?>
--EXPECT--
-- clean transactional release keeps the slot
  row 1 is free
  next coroutine: same connection, in transaction: false
  pool count: 1
-- the rollback fails on a live connection: an unread cursor
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
-- the connection is killed inside a transaction (beginTransaction)
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
-- the connection is killed inside a transaction (START TRANSACTION)
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
-- the connection is killed while a statement outlives the coroutine
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
-- a coroutine queued for the only slot while its holder's connection is killed
  waiter: another connection
-- XA transaction: ROLLBACK is refused
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
-- completion_type = CHAIN: the rollback opens the next transaction
  row 1 is free
  next coroutine: another connection, in transaction: false
  pool count: 1
Done
