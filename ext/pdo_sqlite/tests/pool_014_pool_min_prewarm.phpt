--TEST--
PDO_SQLite Pool: ATTR_POOL_MIN pre-warms that many connections
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

// The pool pre-warms inside its own constructor, before pdo_pool_create can
// attach the template it builds connections from, so POOL_MIN used to create
// nothing at all whatever it was set to.
foreach ([0, 1, 3] as $min) {
    $pdo = new PDO("sqlite:$dbfile", null, null, [
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_MIN => $min,
        PDO::ATTR_POOL_MAX => 5,
    ]);
    $pool = $pdo->getPool();
    echo "POOL_MIN=$min: count=", $pool->count(),
         " idle=", $pool->idleCount(),
         " active=", $pool->activeCount(), "\n";
    unset($pool, $pdo);
}

// A POOL_MAX below POOL_MIN is raised to it, so the warm-up still runs in full.
$pdo = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 4,
    PDO::ATTR_POOL_MAX => 2,
]);
$pool = $pdo->getPool();
echo "MIN=4 MAX=2: count=", $pool->count(), "\n";
unset($pool, $pdo);

// A pre-warmed connection is a working one, and gets reused rather than
// pushing the pool past what it already holds.
$pdo = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 2,
    PDO::ATTR_POOL_MAX => 5,
]);
$pool = $pdo->getPool();
$pdo->exec("CREATE TABLE t (v INT)");
$pdo->exec("INSERT INTO t VALUES (11)");
echo "query on warm conn: ", $pdo->query("SELECT v FROM t")->fetchColumn(), "\n";
echo "after use: count=", $pool->count(), "\n";
unset($pool, $pdo);

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
POOL_MIN=0: count=0 idle=0 active=0
POOL_MIN=1: count=1 idle=1 active=0
POOL_MIN=3: count=3 idle=3 active=0
MIN=4 MAX=2: count=4
query on warm conn: 11
after use: count=2
Done
