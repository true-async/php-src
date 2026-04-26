--TEST--
PDO_SQLite Pool: UDF/aggregate/collation registry on template
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

// POOL_MIN=0 keeps the pool empty so registrations made now apply to every
// slot when it is lazily created on first acquire.
$pdo = Pdo\Sqlite::connect($dsn, null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 2,
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
]);

// 1. UDF: scalar function visible to every slot.
$pdo->createFunction('php_upper', fn(string $s) => strtoupper($s), 1);

// 2. Aggregate.
$pdo->createAggregate('php_sum',
    function (?int $acc, int $rownum, int $val): int { return ($acc ?? 0) + $val; },
    function (?int $acc, int $rownum): int { return $acc ?? 0; },
    1
);

// 3. Collation: reverse-string compare.
$pdo->createCollation('reverse', fn(string $a, string $b) => strcmp(strrev($a), strrev($b)));

// 4. Duplicate registration must throw.
try {
    $pdo->createFunction('php_upper', 'strtoupper', 1);
    echo "UNEXPECTED: duplicate accepted\n";
} catch (PDOException $e) {
    echo "DUP rejected: ", $e->getMessage(), "\n";
}

// 5. Same name, different arity is a separate function.
$pdo->createFunction('php_upper', fn(string $s, int $n) => str_repeat(strtoupper($s), $n), 2);

// Schema (single coroutine, single slot, BEGIN/COMMIT exercises pool too).
$pdo->beginTransaction();
$pdo->exec("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT, val TEXT)");
$pdo->exec("INSERT INTO t (val) VALUES ('alpha'), ('bravo'), ('charlie')");
$pdo->commit();

// 6. Run UDF/aggregate/collation through two coroutines so we exercise more
// than one slot.
$tasks = [];
for ($i = 0; $i < 2; $i++) {
    $tasks[] = spawn(function () use ($pdo, $i) {
        $rows = $pdo->query("SELECT php_upper(val) AS u FROM t ORDER BY val COLLATE reverse")
                    ->fetchAll();
        $sum  = $pdo->query("SELECT php_sum(id) AS s FROM t")->fetch();
        $two  = $pdo->query("SELECT php_upper('hi', 3) AS r")->fetch();
        return ["coro $i", $rows, $sum['s'], $two['r']];
    });
}

foreach ($tasks as $t) {
    [$tag, $rows, $sum, $two] = await($t);
    echo "$tag: upper=[", implode(',', array_column($rows, 'u')), "] sum=$sum two=$two\n";
}

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
DUP rejected: PDO_SQLite: function "php_upper" with 1 argument(s) is already registered on the pool template
coro 0: upper=[ALPHA,CHARLIE,BRAVO] sum=6 two=HIHIHI
coro 1: upper=[ALPHA,CHARLIE,BRAVO] sum=6 two=HIHIHI
Done
