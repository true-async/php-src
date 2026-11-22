--TEST--
PDO_SQLite Pool: a failing ATTR_POOL_INIT_SQL statement fails the connect with the driver's error
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

function show(Throwable $e): void {
    for ($n = 0; $e !== null; $e = $e->getPrevious(), $n++) {
        echo str_repeat('  ', $n), get_class($e), ': ', $e->getMessage(), "\n";
    }
}

// The second statement fails: the slot is not configured, so it must not be
// handed out, and every attempt reports why instead of hanging on a slot
// that never comes back.
$pdo = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MAX => 1,
    PDO::ATTR_POOL_INIT_SQL => ['PRAGMA foreign_keys = ON', 'SELECT no_such_function()'],
]);

for ($i = 0; $i < 2; $i++) {
    try {
        await(spawn(fn () => $pdo->query('SELECT 1')->fetchColumn()));
        echo "attempt $i: no exception\n";
    } catch (Throwable $e) {
        echo "attempt $i:\n";
        show($e);
    }
}

echo "slots: ", $pdo->getPool()->count(), "\n";

// With POOL_MIN the first connect runs in the constructor, which then fails.
try {
    new PDO("sqlite:$dbfile", null, null, [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_MIN => 1,
        PDO::ATTR_POOL_INIT_SQL => ['SELECT no_such_function()'],
    ]);
    echo "pre-warm: no exception\n";
} catch (Throwable $e) {
    echo "pre-warm:\n";
    show($e);
}

// A statement that leaves a transaction open would pin every slot it builds.
$txn = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => ['BEGIN'],
]);
try {
    await(spawn(fn () => $txn->query('SELECT 1')->fetchColumn()));
    echo "open transaction: no exception\n";
} catch (Throwable $e) {
    echo "open transaction:\n";
    show($e);
}

unset($pdo, $txn);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
attempt 0:
PDOException: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  PDOException: SQLSTATE[HY000] [1] no such function: no_such_function
attempt 1:
PDOException: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  PDOException: SQLSTATE[HY000] [1] no such function: no_such_function
slots: 0
pre-warm:
PDOException: SQLSTATE[HY000] [1] no such function: no_such_function
open transaction:
PDOException: SQLSTATE[HY000]: General error: Failed to acquire connection from pool
  PDOException: SQLSTATE[HY000] [0] PDO::ATTR_POOL_INIT_SQL left a transaction open
Done
