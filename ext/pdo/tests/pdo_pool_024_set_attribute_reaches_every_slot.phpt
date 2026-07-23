--TEST--
PDO Pool: setAttribute() must reach every slot, not only the one it was dispatched to
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

// setAttribute() dispatches to whichever slot is bound right now, so slots
// created later kept the value they were born with. For ATTR_AUTOCOMMIT that
// loses data: coroutines running on a fresh slot commit what their rollBack()
// was supposed to undo, while getAttribute() keeps reporting false.

$pdo = PDOPoolTest::poolFactory(4);
$pdo->setAttribute(PDO::ATTR_AUTOCOMMIT, false);
var_dump($pdo->getAttribute(PDO::ATTR_AUTOCOMMIT));

$seen = [];
$coros = [];
for ($i = 0; $i < 4; $i++) {
    $coros[] = spawn(function () use ($pdo, &$seen) {
        $seen[] = (int) $pdo->query("SELECT @@session.autocommit")->fetchColumn();
        // autocommit=0 opens an implicit transaction; close it so the slot
        // goes back clean and the script does not exit holding locks.
        $pdo->exec("ROLLBACK");
    });
}
foreach ($coros as $c) {
    await($c);
}

sort($seen);
echo "session autocommit per slot: ", implode(",", $seen), "\n";

echo "Done\n";
?>
--EXPECT--
bool(false)
session autocommit per slot: 0,0,0,0
Done
