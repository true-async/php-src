--TEST--
PDO Pool: a statement that returns rows through exec() or ATTR_POOL_INIT_SQL leaves the slot usable
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

// exec() and the init SQL cannot hand rows to anyone. On a pool slot the rows
// must be read off the connection before the call returns: the slot goes to
// another coroutine, and MySQL refuses every command (2014) while a result is
// unread.

function attempt(string $label, callable $f): void {
    try {
        $shown = preg_replace('/\s+/', ' ', var_export($f(), true));
        echo $label, ": ", $shown, "\n";
    } catch (Throwable $e) {
        echo $label, ": ", get_class($e), " ",
            preg_replace('/^(SQLSTATE\[\w+\](?: \[\d+\])?: [^:]+: \d+).*$/s', '$1', $e->getMessage());
        if ($e->getPrevious()) {
            echo " <- ", preg_replace('/^(SQLSTATE\[\w+\] \[\d+\]).*$/s', '$1', $e->getPrevious()->getMessage());
        }
        echo "\n";
    }
}

$admin = PDOPoolTest::factory();
$admin->exec("DROP PROCEDURE IF EXISTS pdo_pool_032_proc");
$admin->exec("CREATE PROCEDURE pdo_pool_032_proc() BEGIN SELECT 1 AS a; SELECT 2 AS b; END");
$admin->exec("DROP TABLE IF EXISTS pdo_pool_032");
$admin->exec("CREATE TABLE pdo_pool_032 (id INT PRIMARY KEY, v INT) ENGINE=InnoDB");
$admin->exec("INSERT INTO pdo_pool_032 VALUES (1, 0)");

// The server reports this error after it has sent the first row. A server
// that reports it in the header instead would make the cases below pass
// without the fix, so the test checks it first.
$failing = "SELECT IF(n = 2, (SELECT 1 UNION SELECT 2), n) FROM (SELECT 1 AS n UNION ALL SELECT 2 UNION ALL SELECT 3) t";
$probe = PDOPoolTest::factory([Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false]);
$stmt = $probe->query($failing);
attempt("precondition, first row", fn () => $stmt->fetchColumn());
attempt("precondition, second row", fn () => $stmt->fetchColumn());
unset($stmt, $probe);

echo "-- init SQL, rows returned by the last statement\n";
$pdo = PDOPoolTest::poolFactory(1, [PDO::ATTR_POOL_INIT_SQL => ["SET @baseline = 'init'", "SELECT 1"]]);
attempt("first coroutine", fn () => await(spawn(fn () => $pdo->query("SELECT @baseline")->fetchColumn())));
attempt("second coroutine", fn () => await(spawn(fn () => $pdo->query("SELECT @baseline")->fetchColumn())));

echo "-- init SQL, rows returned by a statement in the middle, and by a CALL\n";
$pdo = PDOPoolTest::poolFactory(1, [PDO::ATTR_POOL_INIT_SQL => ["SELECT 1", "CALL pdo_pool_032_proc()", "SET @baseline = 'init'"]]);
attempt("first coroutine", fn () => await(spawn(fn () => $pdo->query("SELECT @baseline")->fetchColumn())));

echo "-- init SQL with rows is replayed after a reset on the same connection\n";
$pdo = PDOPoolTest::poolFactory(1, [PDO::ATTR_POOL_INIT_SQL => ["SET @baseline = 'init'", "SHOW VARIABLES LIKE 'sql_mode'"]]);
$seen = new stdClass(); // an object: arrow functions capture by value
attempt("dirtying coroutine", fn () => await(spawn(function () use ($pdo, $seen) {
    $seen->conn = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
    return $pdo->exec("SET @baseline = 'coroutine'");
})));
attempt("after the reset", fn () => await(spawn(fn () => [
    $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $seen->conn ? 'same connection' : 'another connection',
    $pdo->query("SELECT @baseline")->fetchColumn(),
])));

echo "-- init SQL whose error arrives after the first row\n";
$pdo = PDOPoolTest::poolFactory(1, [PDO::ATTR_POOL_INIT_SQL => [$failing]]);
attempt("first coroutine", fn () => await(spawn(fn () => $pdo->query("SELECT 1")->fetchColumn())));

echo "-- exec() of a statement that returns rows\n";
$pdo = PDOPoolTest::poolFactory(1);
$seen = new stdClass(); // an object: arrow functions capture by value
attempt("exec, then a query in the same coroutine", fn () => await(spawn(function () use ($pdo, $seen) {
    $seen->conn = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
    return [$pdo->exec("SELECT 1"), $pdo->query("SELECT 2")->fetchColumn()];
})));
attempt("next coroutine", fn () => await(spawn(fn () =>
    $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $seen->conn ? 'same connection' : 'another connection')));

echo "-- exec() of several result sets\n";
foreach (["SELECT 1; SELECT 2", "CALL pdo_pool_032_proc()", "SHOW VARIABLES LIKE 'sql_mode'"] as $sql) {
    $pdo = PDOPoolTest::poolFactory(1);
    $seen = new stdClass(); // an object: arrow functions capture by value
    attempt($sql, fn () => await(spawn(function () use ($pdo, $sql, $seen) {
        $seen->conn = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
        return $pdo->exec($sql);
    })));
    attempt("  next coroutine", fn () => await(spawn(fn () =>
        $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $seen->conn ? 'same connection' : 'another connection')));
}

echo "-- exec() of rows inside a transaction\n";
$pdo = PDOPoolTest::poolFactory(1);
attempt("commit", fn () => await(spawn(function () use ($pdo) {
    $pdo->beginTransaction();
    $pdo->exec("UPDATE pdo_pool_032 SET v = 1 WHERE id = 1");
    $pdo->exec("SELECT v FROM pdo_pool_032");
    return $pdo->commit();
})));
// A second connection sees whether the row is still locked.
$admin->exec("SET SESSION innodb_lock_wait_timeout = 1");
attempt("another connection updates the row", fn () => $admin->exec("UPDATE pdo_pool_032 SET v = v + 1 WHERE id = 1"));
attempt("value", fn () => $admin->query("SELECT v FROM pdo_pool_032 WHERE id = 1")->fetchColumn());

echo "-- exec() whose error arrives after the first row\n";
$pdo = PDOPoolTest::poolFactory(1);
$seen = new stdClass(); // an object: arrow functions capture by value
attempt("exec", fn () => await(spawn(function () use ($pdo, $failing, $seen) {
    $seen->conn = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();
    return $pdo->exec($failing);
})));
attempt("next coroutine", fn () => await(spawn(fn () => [
    $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $seen->conn ? 'same connection' : 'another connection',
    $pdo->query("SELECT 3")->fetchColumn(),
])));

echo "-- the same error behind a result that is not a result set\n";
$pdo = PDOPoolTest::poolFactory(1);
attempt("exec", fn () => await(spawn(fn () => $pdo->exec("DO 0; " . $failing))));
attempt("next coroutine", fn () => await(spawn(fn () => $pdo->query("SELECT 3")->fetchColumn())));

echo "-- the same error inside a transaction\n";
// The error leaves the server's status unread: the transaction must still
// be known to the pool, so the coroutine can roll it back and no other one
// inherits it.
$pdo = PDOPoolTest::poolFactory(1);
attempt("rollBack after the error", fn () => await(spawn(function () use ($pdo, $failing) {
    $pdo->beginTransaction();
    $pdo->exec("UPDATE pdo_pool_032 SET v = 100 WHERE id = 1");
    try {
        $pdo->exec($failing);
    } catch (PDOException $e) {
        echo "  caught: ", preg_replace('/^(SQLSTATE\[\w+\]).*$/s', '$1', $e->getMessage()), "\n";
    }
    return $pdo->rollBack();
})));
attempt("next coroutine in a transaction", fn () => await(spawn(fn () => $pdo->inTransaction())));
attempt("another connection updates the row", fn () => $admin->exec("UPDATE pdo_pool_032 SET v = v + 1 WHERE id = 1"));
attempt("value", fn () => $admin->query("SELECT v FROM pdo_pool_032 WHERE id = 1")->fetchColumn());

unset($pdo);
$admin->exec("DROP PROCEDURE pdo_pool_032_proc");
$admin->exec("DROP TABLE pdo_pool_032");
echo "Done\n";
?>
--EXPECT--
precondition, first row: 1
precondition, second row: PDOException SQLSTATE[21000]: Cardinality violation: 1242
-- init SQL, rows returned by the last statement
first coroutine: 'init'
second coroutine: 'init'
-- init SQL, rows returned by a statement in the middle, and by a CALL
first coroutine: 'init'
-- init SQL with rows is replayed after a reset on the same connection
dirtying coroutine: 0
after the reset: array ( 0 => 'same connection', 1 => 'init', )
-- init SQL whose error arrives after the first row
first coroutine: PDOException SQLSTATE[HY000]: General error: Failed to acquire connection from pool <- SQLSTATE[21000] [1242]
-- exec() of a statement that returns rows
exec, then a query in the same coroutine: array ( 0 => 0, 1 => 2, )
next coroutine: 'same connection'
-- exec() of several result sets
SELECT 1; SELECT 2: 0
  next coroutine: 'same connection'
CALL pdo_pool_032_proc(): 0
  next coroutine: 'same connection'
SHOW VARIABLES LIKE 'sql_mode': 0
  next coroutine: 'same connection'
-- exec() of rows inside a transaction
commit: true
another connection updates the row: 1
value: 2
-- exec() whose error arrives after the first row
exec: PDOException SQLSTATE[21000]: Cardinality violation: 1242
next coroutine: array ( 0 => 'same connection', 1 => 3, )
-- the same error behind a result that is not a result set
exec: PDOException SQLSTATE[21000]: Cardinality violation: 1242
next coroutine: 3
-- the same error inside a transaction
  caught: SQLSTATE[21000]
rollBack after the error: true
next coroutine in a transaction: false
another connection updates the row: 1
value: 3
Done
