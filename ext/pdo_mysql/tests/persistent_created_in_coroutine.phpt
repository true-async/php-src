--TEST--
PDO_MySQL: a persistent connection cannot be opened inside a coroutine
--EXTENSIONS--
pdo_mysql
true_async
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
use function Async\spawn;
use function Async\await;
use function Async\await_all;

// Built without the shared test includes, which start the reactor themselves.
function persistent(array $options = []): PDO {
    return new PDO(
        getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test',
        getenv('PDO_MYSQL_TEST_USER') ?: 'root',
        getenv('PDO_MYSQL_TEST_PASS') ?: '',
        $options + [PDO::ATTR_PERSISTENT => true, PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
}

// Two coroutines opening the same persistent connection would share one
// connection, and the constructor's lookup and registration are split by a
// suspending connect. Only the main flow may open one; a pool serves coroutines.
$open = function (int $i) {
    try {
        persistent();
        return "c$i opened";
    } catch (PDOException $e) {
        return "c$i: " . $e->getMessage();
    }
};
[$results] = await_all([spawn($open, 1), spawn($open, 2)]);
echo implode("\n", $results), "\n";

echo "non-persistent: ", await(spawn(fn () =>
    persistent([PDO::ATTR_PERSISTENT => false])->query("SELECT 1")->fetchColumn())), "\n";

file(__FILE__); // the main flow, with the reactor running
echo "main: ", persistent()->query("SELECT 1")->fetchColumn(), "\n";
echo "done\n";
?>
--EXPECT--
c1: PDO::ATTR_PERSISTENT cannot be used inside a coroutine; use PDO::ATTR_POOL_ENABLED
c2: PDO::ATTR_PERSISTENT cannot be used inside a coroutine; use PDO::ATTR_POOL_ENABLED
non-persistent: 1
main: 1
done
