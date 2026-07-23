--TEST--
PDO_SQLite Pool: driver-specific constructor options reach pooled connections
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pool = [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 0,
    PDO::ATTR_POOL_MAX => 2,
];

// Seed the file with a writable handle.
$seed = new PDO("sqlite:$dbfile", null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]);
$seed->exec("CREATE TABLE t (v INT)");
unset($seed);

// The pool factory used to call the driver with no options at all, so nothing
// the constructor asked for reached a slot. For the SSL_* family that meant a
// silent downgrade to plaintext; here it is visible as a read-only handle that
// happily accepts writes.
$ro = new PDO("sqlite:$dbfile", null, null,
    $pool + [Pdo\Sqlite::ATTR_OPEN_FLAGS => Pdo\Sqlite::OPEN_READONLY]);

echo "read on read-only pooled conn: ", (int) $ro->query("SELECT COUNT(*) FROM t")->fetchColumn(), "\n";

try {
    $ro->exec("INSERT INTO t VALUES (1)");
    echo "UNEXPECTED: write accepted on a read-only pooled connection\n";
} catch (PDOException $e) {
    echo "write refused: ", $e->getCode(), "\n";
}
unset($ro);

// Without the flag the same pooled DSN stays writable.
$rw = new PDO("sqlite:$dbfile", null, null, $pool);
$rw->exec("INSERT INTO t VALUES (2)");
echo "write on default pooled conn: ", $rw->query("SELECT v FROM t")->fetchColumn(), "\n";
unset($rw);

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
read on read-only pooled conn: 0
write refused: HY000
write on default pooled conn: 2
Done
