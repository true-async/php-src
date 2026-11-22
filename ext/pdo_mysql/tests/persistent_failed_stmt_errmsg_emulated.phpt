--TEST--
PDO_MySQL: a statement error on a persistent connection is freed with the allocator it came from (emulated prepares)
--EXTENSIONS--
pdo_mysql
--SKIPIF--
<?php
$dsn = getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test';
try {
    new PDO($dsn, getenv('PDO_MYSQL_TEST_USER') ?: 'root', getenv('PDO_MYSQL_TEST_PASS') ?: '');
} catch (PDOException $e) {
    die('skip could not connect');
}
?>
--FILE--
<?php
// Built without the shared test includes, which start the async reactor: the
// test covers a persistent connection used before it starts.
$db = new PDO(
    getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test',
    getenv('PDO_MYSQL_TEST_USER') ?: 'root',
    getenv('PDO_MYSQL_TEST_PASS') ?: '',
    [
        PDO::ATTR_PERSISTENT => true,
        PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
        PDO::ATTR_EMULATE_PREPARES => true,
    ]
);

// Fails at execute, after the statement exists, so its own error is set.
$failing = "SELECT (SELECT 1 UNION SELECT 2)";

var_dump($db->query($failing));
var_dump($db->errorInfo()[2] !== null);

// The next call releases the failed statement the handle kept.
var_dump($db->query("SELECT 1")->fetchColumn());
echo "after clear\n";

// A statement that fails twice replaces its first message.
$stmt = $db->prepare($failing);
var_dump($stmt->execute());
var_dump($stmt->execute());
var_dump($stmt->errorInfo()[1]);
unset($stmt);
echo "after statement\n";

var_dump($db->query($failing));
unset($db);
echo "after unset\n";
?>
--EXPECT--
bool(false)
bool(true)
int(1)
after clear
bool(false)
bool(false)
int(1242)
after statement
bool(false)
after unset
