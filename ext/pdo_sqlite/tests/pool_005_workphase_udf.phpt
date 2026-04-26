--TEST--
PDO_SQLite Pool: pre-warm sync + freeze rejects late registration
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');
$dsn = "sqlite:file:" . $dbfile . "?cache=shared";

// POOL_MIN=2 forces pre-warm: two slots are created before any registration.
// before_acquire applies the template registry the first time each slot is
// handed to a coroutine.
$pdo = Pdo\Sqlite::connect($dsn, null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 2,
    PDO::ATTR_POOL_MAX => 2,
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
]);

// Template registration AFTER pre-warm — must work because the pool has not
// yet entered work phase (no acquire has happened).
$pdo->createFunction('shared_upper', fn(string $s) => strtoupper($s), 1);

$pdo->exec("CREATE TABLE t (val TEXT)");
$pdo->exec("INSERT INTO t (val) VALUES ('alpha'), ('bravo')");

// First exec acquired a slot → template is now frozen. Any further
// createFunction/createCollation must throw.
try {
    $pdo->createFunction('late_udf', fn() => 0, 0);
    echo "UNEXPECTED: late createFunction accepted\n";
} catch (PDOException $e) {
    echo "FROZEN func rejected\n";
}

try {
    $pdo->createCollation('late_coll', fn($a, $b) => 0);
    echo "UNEXPECTED: late createCollation accepted\n";
} catch (PDOException $e) {
    echo "FROZEN coll rejected\n";
}

// shared_upper still works on every slot — confirm via two coroutines.
$tasks = [];
for ($i = 0; $i < 2; $i++) {
    $tasks[] = spawn(function () use ($pdo, $i) {
        $row = $pdo->query("SELECT shared_upper(val) AS u FROM t LIMIT 1")->fetch();
        return "coro $i: " . $row['u'];
    });
}
foreach ($tasks as $t) {
    echo await($t), "\n";
}

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
FROZEN func rejected
FROZEN coll rejected
coro 0: ALPHA
coro 1: ALPHA
Done
