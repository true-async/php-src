--TEST--
PDO Pool: cleaning a slot must not drop what the constructor configured
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

// Clearing a slot resets the session to the server's *global* defaults, which
// throws away what the DSN asked for. Restoring only the state the leak test
// looks at would leave the connection quietly misconfigured: pointed at
// another schema, or speaking a charset the client does not think it speaks,
// while the pool reports the slot as clean.

$pdo = PDOPoolTest::poolFactory(1);   // one slot, so reuse is guaranteed

$before = [];
await(spawn(function () use ($pdo, &$before) {
    $before['schema']  = $pdo->query("SELECT DATABASE()")->fetchColumn();
    $before['charset'] = $pdo->query("SELECT @@session.character_set_client")->fetchColumn();
    $before['conn']    = $pdo->query("SELECT CONNECTION_ID()")->fetchColumn();

    // Dirty the slot, and move it off the DSN's schema while we are at it.
    $pdo->exec("SET @secret = 'coroutine-A-data'");
    $pdo->exec("USE mysql");
}));

await(spawn(function () use ($pdo, $before) {
    // Cleaned, not thrown away: the physical connection is the same one.
    echo "same connection: ", var_export(
        $pdo->query("SELECT CONNECTION_ID()")->fetchColumn() === $before['conn'], true), "\n";

    echo "state cleared: ", var_export($pdo->query("SELECT @secret")->fetchColumn() === null, true), "\n";
    echo "schema restored: ", var_export(
        $pdo->query("SELECT DATABASE()")->fetchColumn() === $before['schema'], true), "\n";
    echo "charset restored: ", var_export(
        $pdo->query("SELECT @@session.character_set_client")->fetchColumn() === $before['charset'], true), "\n";
}));

echo "Done\n";
?>
--EXPECT--
same connection: true
state cleared: true
schema restored: true
charset restored: true
Done
