--TEST--
PDO Pool: Pdo\Mysql::getWarningCount() reads the bound slot instead of the template
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

// In pool mode the userland PDO is a template with driver_data == NULL, so
// reading warnings off it segfaulted. The count belongs to whichever slot is
// currently bound to this coroutine; with no slot there is nothing to report.

$pdo = PDOPoolTest::poolFactory(3, [], Pdo\Mysql::class);

echo "no slot bound yet: ", $pdo->getWarningCount(), "\n";

// A live statement pins the slot, so its warning is visible.
$stmt = $pdo->query("SELECT 1/0 AS x");
$stmt->fetchAll();
echo "slot pinned by stmt: ", $pdo->getWarningCount(), "\n";

unset($stmt);
echo "slot back in pool: ", $pdo->getWarningCount(), "\n";

// An open transaction pins the slot too.
$pdo->beginTransaction();
$pdo->query("SELECT 1/0 AS x")->fetchAll();
echo "slot pinned by txn: ", $pdo->getWarningCount(), "\n";
$pdo->rollBack();

echo "Done\n";
?>
--EXPECT--
no slot bound yet: 0
slot pinned by stmt: 1
slot back in pool: 0
slot pinned by txn: 1
Done
