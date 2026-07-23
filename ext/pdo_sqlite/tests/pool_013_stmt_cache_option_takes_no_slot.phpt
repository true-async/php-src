--TEST--
PDO_SQLite Pool: ATTR_POOL_STMT_CACHE_SIZE must not open a connection at construction
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$base = [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 2,
];

// The pool attributes are consumed by pdo_pool_create(); dispatching one to
// the driver as an ordinary attribute would acquire a slot to set something
// no driver understands. STMT_CACHE_SIZE used to fall outside that skip range.
foreach ([['without stmt cache', $base],
          ['with stmt cache   ', $base + [PDO::ATTR_POOL_STMT_CACHE_SIZE => 8]]] as [$label, $opts]) {
    $pdo = new PDO("sqlite:$dbfile", null, null, $opts);
    $pool = $pdo->getPool();
    echo $label, ": count=", $pool->count(),
         " idle=", $pool->idleCount(),
         " active=", $pool->activeCount(), "\n";
    unset($pool, $pdo);
}

// The option still yields a working pool.
$pdo = new PDO("sqlite:$dbfile", null, null, $base + [PDO::ATTR_POOL_STMT_CACHE_SIZE => 8]);
$pdo->exec("CREATE TABLE t (v INT)");
$pdo->exec("INSERT INTO t VALUES (7)");
$stmt = $pdo->prepare("SELECT v FROM t WHERE v = ?");
$stmt->execute([7]);
echo "query through cached stmt: ", $stmt->fetchColumn(), "\n";
unset($stmt, $pdo);

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
without stmt cache: count=0 idle=0 active=0
with stmt cache   : count=0 idle=0 active=0
query through cached stmt: 7
Done
