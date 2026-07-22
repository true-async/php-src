--TEST--
PDO Pool: a failed query() must not burn a pool slot
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

// PDO::query() detaches the failed statement from its PDO object. That must not
// be mistaken for "the PDO is gone", or every failed query destroys a pooled
// connection without ever giving the slot back.

$pdo = PDOPoolTest::poolFactory();
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_SILENT);

$seen = [];
for ($i = 0; $i < 6; $i++) {
    $pdo->query("SELECT * FROM definitely_missing_tbl");
    $seen[] = $pdo->getPool()->activeCount();
}

// The stash pins at most one connection; the count must not grow with the loop.
echo "max active: ", max($seen), "\n";

// The pool must still be usable afterwards.
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$row = $pdo->query("SELECT 1 AS n")->fetch(PDO::FETCH_ASSOC);
echo "still works: ", $row['n'], "\n";

echo "Done\n";
?>
--EXPECT--
max active: 1
still works: 1
Done
