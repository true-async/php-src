--TEST--
PDO_SQLite Pool: ATTR_POOL_INIT_SQL configures every physical connection
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

use function Async\spawn;
use function Async\await_all;
use function Async\delay;

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

// A PRAGMA run through the outer handle lands on one slot only; the pool's
// init SQL is the per-connection baseline, so every slot must carry it,
// pre-warmed ones included.
$pdo = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 1,
    PDO::ATTR_POOL_MAX => 3,
    PDO::ATTR_POOL_INIT_SQL => ['PRAGMA foreign_keys = ON', 'PRAGMA cache_size = -4321'],
]);

$tasks = [];
for ($i = 0; $i < 3; $i++) {
    $tasks[] = spawn(function () use ($pdo, $i) {
        // The open transaction pins the slot, so the three coroutines hold three slots.
        $pdo->beginTransaction();
        $fk = $pdo->query('PRAGMA foreign_keys')->fetchColumn();
        $cache = $pdo->query('PRAGMA cache_size')->fetchColumn();
        delay(20);
        $pdo->commit();
        return "fk=$fk cache=$cache";
    });
}

[$results] = await_all($tasks);
foreach ($results as $i => $r) {
    echo "coroutine $i: $r\n";
}

echo "slots: ", $pdo->getPool()->count(), "\n";

// One string is one statement, and reads back as a one-element list.
$one = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => 'PRAGMA foreign_keys = ON',
]);
echo "one statement: ", $one->query('PRAGMA foreign_keys')->fetchColumn(), "\n";
var_dump($one->getAttribute(PDO::ATTR_POOL_INIT_SQL));
var_dump($pdo->getAttribute(PDO::ATTR_POOL_INIT_SQL));

// Statements that return rows are fine: the rows are discarded.
$rows = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => ['PRAGMA busy_timeout = 5000', 'SELECT 1'],
]);
echo "rows discarded: ", $rows->query('PRAGMA busy_timeout')->fetchColumn(), "\n";

// Without a pool the attribute is ignored, like the other pool attributes.
$direct = new PDO("sqlite:$dbfile", null, null, [
    PDO::ATTR_POOL_INIT_SQL => ['PRAGMA foreign_keys = ON'],
]);
echo "no pool: ", $direct->query('PRAGMA foreign_keys')->fetchColumn(), "\n";
var_dump($direct->getAttribute(PDO::ATTR_POOL_INIT_SQL));

unset($pdo, $one, $rows, $direct);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
coroutine 0: fk=1 cache=-4321
coroutine 1: fk=1 cache=-4321
coroutine 2: fk=1 cache=-4321
slots: 3
one statement: 1
array(1) {
  [0]=>
  string(24) "PRAGMA foreign_keys = ON"
}
array(2) {
  [0]=>
  string(24) "PRAGMA foreign_keys = ON"
  [1]=>
  string(25) "PRAGMA cache_size = -4321"
}
rows discarded: 5000
no pool: 0
bool(false)
Done
