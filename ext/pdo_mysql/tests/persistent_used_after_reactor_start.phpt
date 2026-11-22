--TEST--
PDO_MySQL: a persistent connection used after the reactor started
--EXTENSIONS--
pdo_mysql
true_async
--SKIPIF--
<?php
$dsn = getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test';
try {
    new PDO($dsn, getenv('PDO_MYSQL_TEST_USER') ?: 'root', getenv('PDO_MYSQL_TEST_PASS') ?: '');
} catch (PDOException $e) {
    die('skip could not connect');
}
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;

// Built without the shared test includes, which start the reactor themselves.
function persistent(array $options = []): PDO {
    return new PDO(
        getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test',
        getenv('PDO_MYSQL_TEST_USER') ?: 'root',
        getenv('PDO_MYSQL_TEST_PASS') ?: '',
        $options + [PDO::ATTR_PERSISTENT => true, PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
}

$rows = "SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3";

// Created before the reactor, used inside a coroutine.
$pdo = persistent();
echo "coroutine: ", await(spawn(function () use ($pdo, $rows) {
    $pdo->beginTransaction();
    $v = implode(",", $pdo->query($rows)->fetchAll(PDO::FETCH_COLUMN));
    $pdo->rollBack();
    return $v;
})), "\n";

file(__FILE__); // the reactor is running from here on
echo "query: ", $pdo->query("SELECT 1")->fetchColumn(), "\n";

// Released while the main coroutine runs, with a transaction open.
function scoped(): void {
    $pdo = persistent();
    $pdo->beginTransaction();
    file(__FILE__);
}
scoped();
echo "scoped handle released\n";

// An unbuffered statement freed, and one iterated, across the reactor.
$pdo = persistent([Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false]);
$stmt = $pdo->query($rows);
echo "first row: ", $stmt->fetchColumn(), "\n";
file(__FILE__);
$stmt = null;
foreach ($pdo->query($rows) as $row) {
    file(__FILE__);
    echo "row: ", $row[0], "\n";
}

// The same persistent handle picked up twice in one request.
$a = persistent();
$id = $a->query("SELECT CONNECTION_ID()")->fetchColumn();
$a = null;
$b = persistent();
echo "picked up again: ", var_export($b->query("SELECT CONNECTION_ID()")->fetchColumn() === $id, true), "\n";

// Shutdown code that restarts the reactor and then queries.
register_shutdown_function(function () use ($b) {
    file(__FILE__);
    echo "shutdown: ", $b->query("SELECT 2")->fetchColumn(), "\n";
});
echo "done\n";
?>
--EXPECT--
coroutine: 1,2,3
query: 1
scoped handle released
first row: 1
row: 1
row: 2
row: 3
picked up again: true
done
shutdown: 2
