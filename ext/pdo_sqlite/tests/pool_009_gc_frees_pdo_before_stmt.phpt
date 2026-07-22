--TEST--
PDO_SQLite Pool: GC freeing the PDO before its statement (php-async #200)
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

// Two cycles collected in one GC run. The PDO's cycle is buffered first, so the
// dbh is freed before the statement that still borrows a pooled connection —
// stmt->dbh must not be dereferenced on that path.

#[AllowDynamicProperties]
class MyPDO extends PDO { public $keep; }

#[AllowDynamicProperties]
class MyStmt extends PDOStatement { public $b; private function __construct() {} }

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pdo = new MyPDO("sqlite:" . $dbfile, null, null, [
    PDO::ATTR_ERRMODE         => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED    => true,
    PDO::ATTR_POOL_MIN        => 1,
    PDO::ATTR_POOL_MAX        => 4,
    PDO::ATTR_STATEMENT_CLASS => [MyStmt::class],
]);
$pdo->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)");

$a = new stdClass;
$a->pdo = $pdo;
$pdo->keep = $a;                 // cycle 1: PDO

$stmt = $pdo->prepare("INSERT INTO t (v) VALUES (?)");
$stmt->execute(['x']);

$b = new stdClass;
$b->stmt = $stmt;
$stmt->b = $b;                   // cycle 2: statement

unset($a, $pdo, $b, $stmt);

var_dump(gc_collect_cycles() > 0);
echo "survived\n";

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
bool(true)
survived
Done
