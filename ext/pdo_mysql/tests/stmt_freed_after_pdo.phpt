--TEST--
PDO_MySQL: a statement freed after its PDO object does not touch the freed handle
--EXTENSIONS--
pdo_mysql
--SKIPIF--
<?php
require_once __DIR__ . '/inc/mysql_pdo_test.inc';
MySQLPDOTest::skip();
?>
--FILE--
<?php
require_once __DIR__ . '/inc/mysql_pdo_test.inc';

// A cycle the collector never visits is freed at shutdown in creation order:
// the PDO object, with its driver handle, before the statement. Run under
// valgrind or ASan to see a regression.
$pdo = MySQLPDOTest::factory();
$pdo->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
$stmt = $pdo->prepare("SELECT 1");
$stmt->execute();
$cycle = new stdClass();
$cycle->self = $cycle;
$cycle->stmt = $stmt;
$cycle->pdo = $pdo;
unset($stmt, $pdo);
gc_disable();
echo "done\n";
?>
--EXPECT--
done
