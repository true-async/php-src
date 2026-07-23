--TEST--
PDO Pool: session state must be spotted whatever the statement is dressed as
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

// Which statements can leave session state behind is decided from the SQL
// text, so every way of hiding the first keyword is a way of handing state to
// the next coroutine. A leading comment used to defeat the test outright, and
// versioned comments are executed rather than being comments at all.

$forms = [
    'bare'              => "SET SESSION sql_mode='ANSI_QUOTES'",
    'block comment'     => "/* audit */ SET SESSION sql_mode='ANSI_QUOTES'",
    'line comment'      => "-- audit\nSET SESSION sql_mode='ANSI_QUOTES'",
    'hash comment'      => "# audit\nSET SESSION sql_mode='ANSI_QUOTES'",
    'versioned mysql'   => "/*!40101 SET SESSION sql_mode='ANSI_QUOTES' */",
    'versioned mariadb' => "/*M!100000 SET SESSION sql_mode='ANSI_QUOTES' */",
    'second statement'  => "DO 1; SET SESSION sql_mode='ANSI_QUOTES'",
];

foreach ($forms as $label => $sql) {
    $pdo = PDOPoolTest::poolFactory(1);   // one slot, so reuse is guaranteed

    await(spawn(function () use ($pdo, $sql) {
        $pdo->exec($sql);
    }));

    await(spawn(function () use ($pdo, $label) {
        $leaked = str_contains($pdo->query("SELECT @@session.sql_mode")->fetchColumn(), 'ANSI_QUOTES');
        printf("%-18s %s\n", $label, $leaked ? 'LEAKED' : 'clean');
    }));

    unset($pdo);
}

// A stored program's body is not readable from the SQL text, so CALL has to
// count as stateful on its own.
$pdo = PDOPoolTest::poolFactory(1);
$pdo->exec("DROP PROCEDURE IF EXISTS pdo_pool_028_proc");
$pdo->exec("CREATE PROCEDURE pdo_pool_028_proc() BEGIN DO GET_LOCK('pdo_pool_028', 0); END");

await(spawn(function () use ($pdo) {
    $pdo->exec("CALL pdo_pool_028_proc()");
}));

await(spawn(function () use ($pdo) {
    printf("%-18s %s\n", 'lock via CALL',
        $pdo->query("SELECT IS_USED_LOCK('pdo_pool_028')")->fetchColumn() === null ? 'clean' : 'LEAKED');
}));

$pdo->exec("DROP PROCEDURE pdo_pool_028_proc");

echo "Done\n";
?>
--EXPECT--
bare               clean
block comment      clean
line comment       clean
hash comment       clean
versioned mysql    clean
versioned mariadb  clean
second statement   clean
lock via CALL      clean
Done
