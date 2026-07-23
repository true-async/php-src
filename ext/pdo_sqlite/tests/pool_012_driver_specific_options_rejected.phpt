--TEST--
PDO_SQLite Pool: driver-specific constructor options are rejected instead of silently dropped
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pool = [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 2,
];

// pdo_pool_factory() passes options = NULL to the driver factory, so a
// driver-specific option would never reach the connection. Dropping it
// quietly is what makes the MySQL SSL_* case a silent TLS downgrade.
try {
    new PDO("sqlite:$dbfile", null, null,
        $pool + [Pdo\Sqlite::ATTR_OPEN_FLAGS => Pdo\Sqlite::OPEN_READONLY]);
    echo "UNEXPECTED OK: pooled + driver-specific option\n";
} catch (PDOException $e) {
    echo "REJECTED: ", $e->getMessage(), "\n";
}

// Generic (non driver-specific) options stay allowed with a pool.
$pdo = new PDO("sqlite:$dbfile", null, null,
    $pool + [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$pdo->exec("CREATE TABLE t (v INT)");
echo "pooled without driver options: ", $pdo->query("SELECT 1")->fetchColumn(), "\n";
unset($pdo);

// Without a pool the same driver-specific option is untouched.
$direct = new PDO("sqlite:$dbfile", null, null,
    [Pdo\Sqlite::ATTR_OPEN_FLAGS => Pdo\Sqlite::OPEN_READONLY]);
echo "direct with driver options: ", $direct->query("SELECT 1")->fetchColumn(), "\n";
unset($direct);

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
REJECTED: PDO::ATTR_POOL_ENABLED cannot be used with driver-specific option 1000, it would not be applied to pooled connections
pooled without driver options: 1
direct with driver options: 1
Done
