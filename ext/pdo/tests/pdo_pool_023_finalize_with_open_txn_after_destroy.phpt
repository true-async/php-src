--TEST--
PDO Pool: coroutine finalizing with an open transaction survives the PDO being destroyed
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

// A coroutine ending on an open transaction still pins its slot, so the
// release runs from the finalize callback. That release does driver I/O and
// suspends; pdo_pool_destroy() can complete meanwhile and clear binding->dbh.
// The callback must notice instead of dereferencing it.

$pdo = PDOPoolTest::poolFactory(1);
$pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_SILENT);

$a = Async\spawn(function () use ($pdo) {
    $pdo->beginTransaction();   // never committed
});
Async\await($a);

unset($pdo);

echo "survived unset\n";
?>
--EXPECT--
survived unset
