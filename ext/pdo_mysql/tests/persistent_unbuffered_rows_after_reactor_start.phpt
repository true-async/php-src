--TEST--
PDO_MySQL: unbuffered rows of a persistent connection read after the reactor started
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
// Built without the shared test includes, which start the reactor themselves.
function persistent(array $options = []): PDO {
    return new PDO(
        getenv('PDO_MYSQL_TEST_DSN') ?: 'mysql:host=localhost;dbname=test',
        getenv('PDO_MYSQL_TEST_USER') ?: 'root',
        getenv('PDO_MYSQL_TEST_PASS') ?: '',
        $options + [PDO::ATTR_PERSISTENT => true, PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
}

// The rows are already in the stream's buffer when the reactor starts; reading
// them takes the stream's buffer lock, which is request memory.
$pdo = persistent([Pdo\Mysql::ATTR_USE_BUFFERED_QUERY => false]);
$stmt = $pdo->query("SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3");
echo $stmt->fetchColumn(), "\n";
file(__FILE__);
echo $stmt->fetchColumn(), "\n";
echo $stmt->fetchColumn(), "\n";
echo "done\n";
?>
--EXPECT--
1
2
3
done
