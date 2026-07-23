--TEST--
PDO_SQLite Pool: inTransaction() must not take a pool slot
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pdo = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 4,
]);
$pool = $pdo->getPool();

// A transaction pins the slot that opened it, so with no slot bound there is
// nothing to be inside of. This used to acquire one anyway and never give it
// back, which in the main flow pinned it for the whole request.
var_dump($pdo->inTransaction());
echo "after inTransaction(): count=", $pool->count(), " active=", $pool->activeCount(), "\n";

$pdo->beginTransaction();
var_dump($pdo->inTransaction());
echo "inside transaction:    count=", $pool->count(), " active=", $pool->activeCount(), "\n";

$pdo->rollBack();
var_dump($pdo->inTransaction());

// commit()/rollBack() still report a missing transaction rather than opening one.
try {
    $pdo->commit();
    echo "UNEXPECTED OK\n";
} catch (PDOException $e) {
    echo "commit without transaction: ", $e->getMessage(), "\n";
}

unset($pool, $pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
bool(false)
after inTransaction(): count=0 active=0
bool(true)
inside transaction:    count=1 active=1
bool(false)
commit without transaction: There is no active transaction
Done
