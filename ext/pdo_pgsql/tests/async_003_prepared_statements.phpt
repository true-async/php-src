--TEST--
PDO PgSQL concurrent query - prepared statements
--EXTENSIONS--
pdo_pgsql
true_async
--SKIPIF--
<?php
require __DIR__ . '/config.inc';
require dirname(__DIR__, 2) . '/pdo/tests/pdo_test.inc';
PDOTest::skip();
?>
--FILE--
<?php

use function Async\spawn;
use function Async\await;

require_once __DIR__ . "/config.inc";

$dsn = $config['ENV']['PDOTEST_DSN'];
$db = new PDO($dsn);
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
$table_name = "test_pdo_async_003";

// Create test table
$db->exec("DROP TABLE IF EXISTS {$table_name}");
$db->exec("CREATE TABLE {$table_name} (id int, name text, score int)");
$db->exec("INSERT INTO {$table_name} VALUES (1, 'Alice', 95), (2, 'Bob', 87), (3, 'Charlie', 92)");

echo "Testing concurrent prepared statements...\n";

// Test prepared statement in coroutine
$coroutine = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    echo "Coroutine: preparing statement\n";

    $stmt = $db->prepare("SELECT * FROM {$table_name} WHERE name = :name AND score > :score");
    $stmt->execute(['name' => 'Bob', 'score' => 80]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);

    echo "Coroutine: got result id={$row['id']}, name={$row['name']}, score={$row['score']}\n";
});

await($coroutine);
echo "Main: coroutine completed\n";

// Test multiple concurrent prepared queries
echo "Testing multiple concurrent prepared queries...\n";

$coro1 = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $stmt = $db->prepare("SELECT * FROM {$table_name} WHERE id = ?");
    $stmt->execute([1]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    echo "Coro1: {$row['name']}\n";
});

$coro2 = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $stmt = $db->prepare("SELECT * FROM {$table_name} WHERE id = ?");
    $stmt->execute([3]);
    $row = $stmt->fetch(PDO::FETCH_ASSOC);
    echo "Coro2: {$row['name']}\n";
});

await($coro1);
await($coro2);

// Cleanup
$db->exec("DROP TABLE {$table_name}");

echo "OK\n";
?>
--EXPECT--
Testing concurrent prepared statements...
Coroutine: preparing statement
Coroutine: got result id=2, name=Bob, score=87
Main: coroutine completed
Testing multiple concurrent prepared queries...
Coro1: Alice
Coro2: Charlie
OK
