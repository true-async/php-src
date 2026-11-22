--TEST--
PDO Pool PgSQL: a slot whose connection dropped is rebuilt with ATTR_POOL_INIT_SQL, not silently reconnected
--EXTENSIONS--
pdo
pdo_pgsql
true_async
--SKIPIF--
<?php
$dsn = getenv('PDOTEST_PGSQL') ?: 'pgsql:host=localhost;port=5433;dbname=test';
$user = getenv('PDOTEST_PGSQL_USER') ?: 'root';
$pass = getenv('PDOTEST_PGSQL_PASS') ?: 'root';
try {
    $pdo = new PDO($dsn, $user, $pass);
    $pdo->query("SELECT 1");
} catch (Exception $e) {
    die('skip PostgreSQL not available: ' . $e->getMessage());
}
?>
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\delay;

$dsn = getenv('PDOTEST_PGSQL') ?: 'pgsql:host=localhost;port=5433;dbname=test';
$user = getenv('PDOTEST_PGSQL_USER') ?: 'root';
$pass = getenv('PDOTEST_PGSQL_PASS') ?: 'root';

// The healthcheck probes idle slots. Reconnecting a dropped one in place gives
// a fresh session that never ran the init SQL, while the pool still counts it
// as configured: such a slot must be destroyed and built again instead.
$pdo = new PDO($dsn, $user, $pass, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MAX => 1,
    PDO::ATTR_POOL_HEALTHCHECK_INTERVAL => 50,
    PDO::ATTR_POOL_INIT_SQL => ["SET TimeZone = 'Asia/Karachi'"],
]);
$killer = new PDO($dsn, $user, $pass, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);

$pid = await(spawn(function () use ($pdo) {
    echo "fresh: ", $pdo->query("SHOW TimeZone")->fetchColumn(), "\n";
    return $pdo->query("SELECT pg_backend_pid()")->fetchColumn();
}));

$killer->query("SELECT pg_terminate_backend($pid)");

// Long enough for several healthcheck rounds over the idle slot.
delay(600);

await(spawn(function () use ($pdo, $pid) {
    echo "new backend: ", var_export($pdo->query("SELECT pg_backend_pid()")->fetchColumn() !== $pid, true), "\n";
    echo "after reconnect: ", $pdo->query("SHOW TimeZone")->fetchColumn(), "\n";
}));

echo "Done\n";
?>
--EXPECT--
fresh: Asia/Karachi
new backend: true
after reconnect: Asia/Karachi
Done
