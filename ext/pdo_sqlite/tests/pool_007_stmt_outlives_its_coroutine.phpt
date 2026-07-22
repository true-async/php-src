--TEST--
PDO_SQLite Pool: statement outliving the coroutine that acquired its connection (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await;

// The coroutine binding is freed as soon as the coroutine finalizes. A statement
// still holding the connection must take ownership of it there, or the pooled
// connection keeps a back-pointer into the freed binding.

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pdo = new PDO("sqlite:" . $dbfile, null, null, [
    PDO::ATTR_ERRMODE      => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN     => 1,
    PDO::ATTR_POOL_MAX     => 4,
]);
$pdo->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");

$escaped = null;

await(spawn(function () use ($pdo, &$escaped) {
    $escaped = $pdo->prepare("INSERT INTO t (v) VALUES (?)");
    $escaped->execute(['x']);
}));

echo "coroutine finished\n";

unset($escaped);   // statement destroyed after its coroutine is already gone
echo "statement freed\n";

// The connection must be usable again, not leaked or double-released.
$row = $pdo->query("SELECT COUNT(*) AS n FROM t")->fetch(PDO::FETCH_ASSOC);
echo "rows: ", $row['n'], "\n";

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
coroutine finished
statement freed
rows: 1
Done
