--TEST--
PDO Pool: a statement closed or evicted while a CALL has result sets leaves them to the CALL
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

// The statement cache closes statements too: the second of two statements
// with one SQL, and the entry evicted to make room.

$admin = PDOPoolTest::factory();
$admin->exec("DROP PROCEDURE IF EXISTS pdo_pool_037_proc");
$admin->exec("CREATE PROCEDURE pdo_pool_037_proc() BEGIN SELECT 1; SELECT 'second'; END");

$cases = [
    "the second statement of one SQL" => [8, ["SELECT 42", "SELECT 42"]],
    "an evicted statement" => [1, ["SELECT 42", "SELECT 43"]],
    "no statement cache" => [0, ["SELECT 42", "SELECT 43"]],
];

foreach ($cases as $label => [$size, $sqls]) {
    echo "-- $label\n";
    $pdo = PDOPoolTest::poolFactory(1, [
        PDO::ATTR_POOL_STMT_CACHE_SIZE => $size,
        PDO::ATTR_EMULATE_PREPARES => false,
        Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false,
    ]);
    echo await(spawn(function () use ($pdo, $sqls) {
        $first = $pdo->prepare($sqls[0]);
        $first->execute();
        $first->fetchAll();
        $second = $pdo->prepare($sqls[1]);
        $second->execute();
        $second->fetchAll();
        $call = $pdo->prepare("CALL pdo_pool_037_proc()");
        $call->execute();
        $call->fetchAll();
        unset($first, $second);
        $more = $call->nextRowset();
        $result = "CALL " . ($more ? $call->fetchColumn() : "lost its second set");
        while ($call->nextRowset());
        unset($call);
        return "$result, next query " . $pdo->query("SELECT 7")->fetchAll(PDO::FETCH_COLUMN)[0];
    })), "\n";
}

unset($pdo);
$admin->exec("DROP PROCEDURE pdo_pool_037_proc");
echo "Done\n";
?>
--EXPECT--
-- the second statement of one SQL
CALL second, next query 7
-- an evicted statement
CALL second, next query 7
-- no statement cache
CALL second, next query 7
Done
