--TEST--
PDO Pool: a statement dropped with its result unread goes to the statement cache with the connection free
--EXTENSIONS--
pdo
pdo_mysql
true_async
--SKIPIF--
<?php
$pdo_pool_inc_dir = getenv('REDIR_TEST_DIR');
if (false === $pdo_pool_inc_dir) $pdo_pool_inc_dir = __DIR__ . '/';
require_once $pdo_pool_inc_dir . 'inc/pdo_pool_test.inc';
PDOPoolTest::skip();
?>
--FILE--
<?php
$pdo_pool_inc_dir = getenv('REDIR_TEST_DIR');
if (false === $pdo_pool_inc_dir) $pdo_pool_inc_dir = __DIR__ . '/';
require_once $pdo_pool_inc_dir . 'inc/pdo_pool_test.inc';

use function Async\spawn;
use function Async\await;

// Closing a statement reads off its unread rows; caching it must do the same,
// or the connection goes back to the pool still sending them (2014).

$admin = PDOPoolTest::factory();
$admin->exec("SET SESSION innodb_lock_wait_timeout = 1");
$admin->exec("DROP TABLE IF EXISTS pdo_pool_034");
$admin->exec("CREATE TABLE pdo_pool_034 (id INT PRIMARY KEY, v INT) ENGINE=InnoDB");
$admin->exec("INSERT INTO pdo_pool_034 VALUES (1, 0), (2, 0), (3, 0)");
$admin->exec("DROP PROCEDURE IF EXISTS pdo_pool_034_proc");
$admin->exec("CREATE PROCEDURE pdo_pool_034_proc() BEGIN SELECT id FROM pdo_pool_034 ORDER BY id; SELECT 'second'; END");

const ROWS = "SELECT id FROM pdo_pool_034 ORDER BY id";

function pool(bool $buffered = false): PDO {
    return PDOPoolTest::poolFactory(1, [
        PDO::ATTR_POOL_STMT_CACHE_SIZE => 8,
        PDO::ATTR_EMULATE_PREPARES => false,
        Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => $buffered,
    ]);
}

// Reads whole results: a partly read one would be the bug under test.
function connection_id(PDO $pdo): int {
    return (int)$pdo->query("SELECT CONNECTION_ID()")->fetchAll(PDO::FETCH_COLUMN)[0];
}

// Server-side prepares on this connection; the statement is itself cached
// after its first use.
function prepares(PDO $pdo): int {
    $stmt = $pdo->prepare("SHOW SESSION STATUS LIKE 'Com_stmt_prepare'");
    $stmt->execute();
    return (int)$stmt->fetchAll(PDO::FETCH_NUM)[0][1];
}

function attempt(callable $f): string {
    try {
        return $f();
    } catch (PDOException $e) {
        return get_class($e) . " " . preg_replace('/^(SQLSTATE\[\w+\]: [^:]+: \d+).*$/s', '$1', $e->getMessage());
    }
}

// Runs $drop in one coroutine, then reports what the next coroutine gets.
function next_coroutine(PDO $pdo, Closure $drop): string {
    $first = await(spawn(function () use ($pdo, $drop) {
        $id = connection_id($pdo);
        prepares($pdo);
        $drop($pdo);
        return $id;
    }));

    return await(spawn(fn () => attempt(function () use ($pdo, $first) {
        $same = connection_id($pdo) === $first ? "same connection" : "another connection";
        $before = prepares($pdo);
        $stmt = $pdo->prepare(ROWS);
        $stmt->execute();
        $rows = implode(",", $stmt->fetchAll(PDO::FETCH_COLUMN));
        $new = prepares($pdo) - $before;
        return "$same, rows $rows, new prepares $new";
    })));
}

echo "-- one of three rows read\n";
echo next_coroutine(pool(), function (PDO $pdo) {
    $stmt = $pdo->prepare(ROWS);
    $stmt->execute();
    $stmt->fetchColumn();
}), "\n";

echo "-- executed, nothing read\n";
echo next_coroutine(pool(), function (PDO $pdo) {
    $pdo->prepare(ROWS)->execute();
}), "\n";

echo "-- inside a transaction\n";
$pdo = pool();
echo next_coroutine($pdo, function (PDO $pdo) {
    $pdo->beginTransaction();
    $pdo->exec("UPDATE pdo_pool_034 SET v = 1 WHERE id = 1");
    $stmt = $pdo->prepare(ROWS);
    $stmt->execute();
    $stmt->fetchColumn();
}), "\n";
echo "another connection updates the row: ", attempt(fn () =>
    (string)$admin->exec("UPDATE pdo_pool_034 SET v = v + 10 WHERE id = 1")), "\n";
echo "value: ", $admin->query("SELECT v FROM pdo_pool_034 WHERE id = 1")->fetchColumn(), "\n";

echo "-- a second statement still reading on the same connection\n";
echo next_coroutine(pool(), function (PDO $pdo) {
    $done = $pdo->prepare("SELECT 1");
    $done->execute();
    $done->fetchAll();
    $reading = $pdo->prepare(ROWS);
    $reading->execute();
    $reading->fetchColumn();
    unset($done); // cached while the other statement holds the connection
    $reading->fetchAll();
}), "\n";

foreach ([false => "unbuffered", true => "buffered"] as $buffered => $label) {
    echo "-- CALL, $label, first result set partly read\n";
    $pdo = pool($buffered);
    echo await(spawn(fn () => attempt(function () use ($pdo) {
        $stmt = $pdo->prepare("CALL pdo_pool_034_proc()");
        $stmt->execute();
        $stmt->fetchColumn();
        unset($stmt);
        $before = prepares($pdo);
        $stmt = $pdo->prepare("CALL pdo_pool_034_proc()");
        $stmt->execute();
        $sets = [implode(",", $stmt->fetchAll(PDO::FETCH_COLUMN))];
        $stmt->nextRowset();
        $sets[] = implode(",", $stmt->fetchAll(PDO::FETCH_COLUMN));
        unset($stmt);
        return "sets " . implode(" | ", $sets) . ", new prepares " . (prepares($pdo) - $before)
            . ", next query " . $pdo->query("SELECT 7")->fetchAll(PDO::FETCH_COLUMN)[0];
    }))), "\n";
}

foreach ([false => "unbuffered", true => "buffered"] as $buffered => $label) {
    echo "-- cached while another statement's CALL still has result sets, $label\n";
    $pdo = pool($buffered);
    echo await(spawn(fn () => attempt(function () use ($pdo) {
        $done = $pdo->prepare("SELECT 42");
        $done->execute();
        $done->fetchAll();
        $done->closeCursor();
        $call = $pdo->prepare("CALL pdo_pool_034_proc()");
        $call->execute();
        $first = implode(",", $call->fetchAll(PDO::FETCH_COLUMN));
        unset($done); // must not read off the CALL's sets
        $more = $call->nextRowset();
        $second = $more ? implode(",", $call->fetchAll(PDO::FETCH_COLUMN)) : "";
        return "sets $first | $second, nextRowset " . var_export($more, true);
    }))), "\n";
}

unset($pdo);
$admin->exec("DROP PROCEDURE pdo_pool_034_proc");
$admin->exec("DROP TABLE pdo_pool_034");
echo "Done\n";
?>
--EXPECT--
-- one of three rows read
same connection, rows 1,2,3, new prepares 0
-- executed, nothing read
same connection, rows 1,2,3, new prepares 0
-- inside a transaction
same connection, rows 1,2,3, new prepares 0
another connection updates the row: 1
value: 10
-- a second statement still reading on the same connection
same connection, rows 1,2,3, new prepares 0
-- CALL, unbuffered, first result set partly read
sets 1,2,3 | second, new prepares 0, next query 7
-- CALL, buffered, first result set partly read
sets 1,2,3 | second, new prepares 0, next query 7
-- cached while another statement's CALL still has result sets, unbuffered
sets 1,2,3 | second, nextRowset true
-- cached while another statement's CALL still has result sets, buffered
sets 1,2,3 | second, nextRowset true
Done
