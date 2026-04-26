--TEST--
PDO_SQLite Pool: full cycle smoke test (prepare/exec/query/txn/lastInsertId/errorInfo)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

// File-based shared cache so multiple connections see the same data.
$dsn = "sqlite:file:" . $dbfile . "?cache=shared";

$pdo = new PDO($dsn, null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 1,
    PDO::ATTR_POOL_MAX => 2,
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
]);

// Seed schema (single coroutine, single connection, one BEGIN/COMMIT).
$pdo->beginTransaction();
$pdo->exec("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, val TEXT)");
$pdo->commit();
echo "schema ok\n";

// Two coroutines, each performs prepare/execute/fetch + insert + lastInsertId.
$tasks = [];
for ($i = 0; $i < 2; $i++) {
    $tasks[] = spawn(function() use ($pdo, $i) {
        $pdo->beginTransaction();
        $stmt = $pdo->prepare("INSERT INTO t (val) VALUES (?)");
        $stmt->execute(["row-$i"]);
        $rowid = $pdo->lastInsertId();
        $pdo->commit();

        $sel = $pdo->prepare("SELECT val FROM t WHERE id = ?");
        $sel->execute([$rowid]);
        $row = $sel->fetch();
        return [$rowid, $row['val']];
    });
}

foreach ($tasks as $t) {
    $r = await($t);
    echo "inserted id=", $r[0], " val=", $r[1], "\n";
}

// query() path
$stmt = $pdo->query("SELECT COUNT(*) AS n FROM t");
echo "count: ", $stmt->fetch()['n'], "\n";

// errorInfo on a successful op should report no-error SQLSTATE.
$info = $pdo->errorInfo();
echo "errorInfo[0] sqlstate: ", $info[0], "\n";

// Trigger a real error and re-check.
try {
    $pdo->exec("SELECT * FROM nonexistent_table");
    echo "expected error did not fire\n";
} catch (PDOException $e) {
    echo "caught PDOException: ", $e->getCode() ?: 'HY000', "\n";
}

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECTF--
schema ok
inserted id=%d val=row-%d
inserted id=%d val=row-%d
count: 2
errorInfo[0] sqlstate: 00000
caught PDOException: HY000
Done
