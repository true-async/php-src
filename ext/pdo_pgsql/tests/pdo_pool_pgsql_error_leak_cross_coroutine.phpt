--TEST--
PDO Pool PgSQL: error state must not leak from one coroutine to another via pooled connection
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

$dsn = getenv('PDOTEST_PGSQL') ?: 'pgsql:host=localhost;port=5433;dbname=test';
$user = getenv('PDOTEST_PGSQL_USER') ?: 'root';
$pass = getenv('PDOTEST_PGSQL_PASS') ?: 'root';

$pdo = new PDO($dsn, $user, $pass, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 1,
]);

/*
 * Bug scenario:
 * 1. Coroutine A acquires connection, triggers SQL error, finishes (releases conn)
 * 2. Coroutine B acquires SAME connection from pool
 * 3. Coroutine B checks errorCode()/errorInfo() BEFORE running any query
 * 4. Expected: clean state "00000", NOT the stale error from coroutine A
 *
 * Pool max=1 forces both coroutines to use the same physical connection.
 */

// Coroutine A: trigger a SQL error
$coroA = spawn(function() use ($pdo) {
    $result = $pdo->exec("SELECT 1 FROM nonexistent_table_xyz_12345");
    echo "Coro A errorCode: " . $pdo->errorCode() . "\n";
});

await($coroA);

// Coroutine A is done, connection released back to pool.
// Now coroutine B acquires the same connection.

$coroB = spawn(function() use ($pdo) {
    // Check error state BEFORE any query — should be clean
    $code = $pdo->errorCode();
    $info = $pdo->errorInfo();
    echo "Coro B errorCode before query: " . $code . "\n";
    echo "Coro B SQLSTATE before query: " . $info[0] . "\n";

    // Now run a successful query
    $stmt = $pdo->query("SELECT 1 as val");
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    echo "Coro B query result: " . $row['val'] . "\n";
    echo "Coro B errorCode after query: " . $pdo->errorCode() . "\n";
});

await($coroB);
echo "Done\n";
?>
--EXPECT--
Coro A errorCode: 42P01
Coro B errorCode before query: 00000
Coro B SQLSTATE before query: 00000
Coro B query result: 1
Coro B errorCode after query: 00000
Done
