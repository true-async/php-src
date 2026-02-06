--TEST--
PDO PgSQL concurrent query - exec() and lastInsertId()
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
$table_name = "test_pdo_async_005";

// Create test table with serial
$db->exec("DROP TABLE IF EXISTS {$table_name}");
$db->exec("CREATE TABLE {$table_name} (id SERIAL PRIMARY KEY, value text)");

echo "Testing exec() in coroutine...\n";

$coroutine = spawn(function() use ($dsn, $table_name) {
    $db = new PDO($dsn);
    $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

    $affected = $db->exec("INSERT INTO {$table_name} (value) VALUES ('row1')");
    echo "Inserted rows: {$affected}\n";

    $id = $db->lastInsertId("{$table_name}_id_seq");
    echo "Last insert ID valid: " . ($id > 0 ? "yes" : "no") . "\n";

    $affected = $db->exec("INSERT INTO {$table_name} (value) VALUES ('row2'), ('row3')");
    echo "Inserted rows: {$affected}\n";

    return $db->lastInsertId("{$table_name}_id_seq");
});

$lastId = await($coroutine);
echo "Last ID valid: " . ($lastId > 0 ? "yes" : "no") . "\n";

// Verify data
$stmt = $db->query("SELECT COUNT(*) FROM {$table_name}");
echo "Total rows: " . $stmt->fetchColumn() . "\n";

// Cleanup
$db->exec("DROP TABLE {$table_name}");

echo "OK\n";
?>
--EXPECT--
Testing exec() in coroutine...
Inserted rows: 1
Last insert ID valid: yes
Inserted rows: 2
Last ID valid: yes
Total rows: 3
OK
