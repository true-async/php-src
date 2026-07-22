--TEST--
PDO_SQLite Pool: statement outliving the PDO after a lazy scheduler launch (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

// The main flow is promoted into the main coroutine on the fly, the first time
// something needs the scheduler (here: an async stream write). A connection
// acquired before that promotion must stay bound to the same context, or it is
// treated as orphaned afterwards and released to the pool twice.

function work(string $dbfile, string $dir): void
{
    $pdo = new PDO("sqlite:" . $dbfile, null, null, [
        PDO::ATTR_ERRMODE      => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_MIN     => 1,
        PDO::ATTR_POOL_MAX     => 8,
    ]);
    $pdo->exec("CREATE TABLE t (id TEXT PRIMARY KEY, v TEXT)");

    // Kept in a local so the statement, not the PDO, holds the last dbh ref.
    $stmt = $pdo->prepare("INSERT INTO t (id, v) VALUES (:id, :v)");
    $stmt->execute(['id' => 'a', 'v' => 'b']);

    // Launches the scheduler: the binding key must not change under us.
    file_put_contents($dir . '/x.txt', "hi\n");

    // Both locals are destroyed here; the statement is freed last.
}

$dir = sys_get_temp_dir() . '/pdo_pool_200_' . getmypid();
@mkdir($dir, 0775, true);
$dbfile = $dir . '/x.db';

work($dbfile, $dir);
echo "survived\n";

// Same shape again, now with the scheduler already running.
work($dbfile . '2', $dir);
echo file_get_contents($dir . '/x.txt');

@unlink($dbfile);
@unlink($dbfile . '2');
@unlink($dir . '/x.txt');
@rmdir($dir);

echo "Done\n";
?>
--EXPECT--
survived
hi
Done
