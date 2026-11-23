--TEST--
PDO Pool: two live statements of the same SQL go back to the statement cache one at a time
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

// The second statement misses the cache and prepares its own; both come back
// on their dtors. A second entry for one key once made eviction free the
// cached statement while its entry stayed, and the next prepare used it.

$pdo = PDOPoolTest::poolFactory(1, [
    PDO::ATTR_POOL_STMT_CACHE_SIZE => 2,
    PDO::ATTR_EMULATE_PREPARES => false,
]);

echo await(spawn(function () use ($pdo) {
    $a = $pdo->prepare("SELECT 1");
    $b = $pdo->prepare("SELECT 1");
    $a->execute();
    $b->execute();
    $rows = $a->fetchColumn() . $b->fetchColumn();
    unset($a, $b);

    // Fills the cache past capacity: the oldest entry is evicted.
    foreach (["SELECT 2", "SELECT 3"] as $sql) {
        $stmt = $pdo->prepare($sql);
        $stmt->execute();
        $rows .= $stmt->fetchColumn();
        unset($stmt);
    }

    $again = $pdo->prepare("SELECT 1");
    $again->execute();
    return "rows $rows, again " . $again->fetchColumn();
})), "\n";

echo "Done\n";
?>
--EXPECT--
rows 1123, again 1
Done
