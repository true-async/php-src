--TEST--
PDO_SQLite Pool: binding created before the scheduler still releases at main-coroutine end (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;

// A binding created before the scheduler exists gets no coroutine callback at
// creation time. Once the main coroutine appears it must be attached on reuse,
// otherwise nothing releases the slot when the main flow ends and a coroutine
// waiting for the only connection waits forever.

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');
$marker = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_launch_');

$pdo = new PDO("sqlite:" . $dbfile, null, null, [
    PDO::ATTR_ERRMODE      => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN     => 0,
    PDO::ATTR_POOL_MAX     => 1,
]);

// Pre-scheduler: creates the key-0 binding with no coroutine callback.
$pdo->exec("CREATE TABLE t (v TEXT)");

file_put_contents($marker, "launch\n");

// Re-acquire after the launch: this is where the callback must be attached.
$pdo->beginTransaction();
$pdo->exec("INSERT INTO t VALUES ('uncommitted')");

spawn(function () use ($pdo, $dbfile, $marker) {
    // Needs the only slot, so it can only run once the main flow lets go.
    echo "coroutine got conn, rows: ", $pdo->query("SELECT COUNT(*) FROM t")->fetchColumn(), "\n";
    @unlink($dbfile);
    @unlink($marker);
    echo "Done\n";
});

// Main flow ends with the transaction still open; the main coroutine's
// finalize must roll it back and return the connection.
echo "main code ends\n";
?>
--EXPECT--
main code ends
coroutine got conn, rows: 0
Done
