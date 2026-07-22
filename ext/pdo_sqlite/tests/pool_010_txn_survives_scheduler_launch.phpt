--TEST--
PDO_SQLite Pool: transaction opened before the scheduler launch survives it (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

// The scheduler promotes the running main flow into the main coroutine on the
// first async operation. If the pool keyed its binding by coroutine identity,
// the key would change mid-transaction and commit() would no longer find it.

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');
$marker = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_launch_');

$pdo = new PDO("sqlite:" . $dbfile, null, null, [
    PDO::ATTR_ERRMODE      => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN     => 0,
    PDO::ATTR_POOL_MAX     => 4,
]);
$pdo->exec("CREATE TABLE t (v TEXT)");

$pdo->beginTransaction();
$pdo->exec("INSERT INTO t VALUES ('x')");

// Launches the scheduler in the middle of the open transaction.
file_put_contents($marker, "launch\n");

$pdo->commit();
echo "commit ok\n";

echo "rows: ", $pdo->query("SELECT COUNT(*) FROM t")->fetchColumn(), "\n";

unset($pdo);
@unlink($dbfile);
@unlink($marker);

echo "Done\n";
?>
--EXPECT--
commit ok
rows: 1
Done
