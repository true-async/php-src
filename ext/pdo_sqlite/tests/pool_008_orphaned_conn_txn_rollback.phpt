--TEST--
PDO_SQLite Pool: open transaction on an orphaned connection is rolled back (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await;

// A statement outlives the coroutine that began a transaction. Once the binding
// is gone nobody can commit it, so releasing the connection must roll it back
// rather than pin it out of the pool forever.

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pdo = new PDO("sqlite:" . $dbfile, null, null, [
    PDO::ATTR_ERRMODE      => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN     => 1,
    PDO::ATTR_POOL_MAX     => 2,
]);
$pdo->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");

$escaped = null;

await(spawn(function () use ($pdo, &$escaped) {
    $pdo->beginTransaction();
    $escaped = $pdo->prepare("INSERT INTO t (v) VALUES (?)");
    $escaped->execute(['uncommitted']);
    // No commit, no rollback: the coroutine just ends.
}));

echo "coroutine finished\n";

unset($escaped);
echo "statement freed\n";

// Rolled back, so the row must be gone, and the pool must still hand out conns.
$row = $pdo->query("SELECT COUNT(*) AS n FROM t")->fetch(PDO::FETCH_ASSOC);
echo "rows: ", $row['n'], "\n";

$pdo->exec("INSERT INTO t (v) VALUES ('after')");
$row = $pdo->query("SELECT COUNT(*) AS n FROM t")->fetch(PDO::FETCH_ASSOC);
echo "rows after reuse: ", $row['n'], "\n";

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
coroutine finished
statement freed
rows: 0
rows after reuse: 1
Done
