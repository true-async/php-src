--TEST--
PDO_MySQL: closing a statement leaves another statement's pending result to it
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

// While a statement's result is still arriving, closing another statement
// must neither read that result nor leave its own server-side statement open.

$db = MySQLPDOTest::factory();
$db->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
$db->setAttribute(Pdo\Mysql::ATTR_USE_BUFFERED_QUERY, false);
$db->exec("DROP PROCEDURE IF EXISTS stmt_close_foreign_proc");
$db->exec("CREATE PROCEDURE stmt_close_foreign_proc() BEGIN SELECT 1; SELECT 'second'; END");

// exec() is a text query: it neither prepares nor closes a statement.
function stmt_closes(PDO $db): int {
    $db->exec("SELECT VARIABLE_VALUE INTO @closes FROM information_schema.SESSION_STATUS WHERE VARIABLE_NAME = 'COM_STMT_CLOSE'");
    $db->setAttribute(PDO::ATTR_EMULATE_PREPARES, true);
    $n = (int)$db->query("SELECT @closes")->fetchColumn();
    $db->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
    return $n;
}

echo "-- closed while a CALL has result sets\n";
$before = stmt_closes($db);
$other = $db->prepare("SELECT 42");
$other->execute();
$other->fetchAll();
$call = $db->prepare("CALL stmt_close_foreign_proc()");
$call->execute();
$call->fetchAll();
unset($other);
$more = $call->nextRowset();
echo "CALL ", $more ? $call->fetchColumn() : "lost its second set", "\n";
while ($call->nextRowset());
unset($call);
echo "server-side closes: ", stmt_closes($db) - $before, "\n";

echo "-- closed while another statement's rows are arriving\n";
$before = stmt_closes($db);
$other = $db->prepare("SELECT 42");
$other->execute();
$other->fetchAll();
$rows = $db->prepare("SELECT 1 UNION SELECT 2 UNION SELECT 3");
$rows->execute();
$read = [$rows->fetchColumn()];
unset($other);
while (($v = $rows->fetchColumn()) !== false) {
    $read[] = $v;
}
unset($rows);
echo "rows ", implode(",", $read), ", server-side closes: ", stmt_closes($db) - $before, "\n";
?>
--CLEAN--
<?php
require_once __DIR__ . '/inc/mysql_pdo_test.inc';
$db = MySQLPDOTest::factory();
$db->exec("DROP PROCEDURE IF EXISTS stmt_close_foreign_proc");
?>
--EXPECT--
-- closed while a CALL has result sets
CALL second
server-side closes: 2
-- closed while another statement's rows are arriving
rows 1,2,3, server-side closes: 2
