--TEST--
PDO_SQLite Pool: methods that still need a single live connection are rejected
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

$pdo = Pdo\Sqlite::connect("sqlite:$dbfile", null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_MIN => 1,
    PDO::ATTR_POOL_MAX => 2,
]);

$probe = function (string $label, callable $fn): void {
    try {
        $fn();
        echo "UNEXPECTED OK: $label\n";
    } catch (PDOException $e) {
        echo "REJECTED: $label -> ", $e->getMessage(), "\n";
    } catch (Throwable $e) {
        echo "OTHER (", $e::class, "): $label -> ", $e->getMessage(), "\n";
    }
};

// These methods bind state to a single sqlite3* (authorizer callback,
// blob handle stream, dynamic library handle) — not yet supported in pool
// mode. createFunction/Aggregate/Collation are now handled via the template
// registry (see pool_004) and are no longer probed here.
$probe('setAuthorizer',   fn() => $pdo->setAuthorizer(fn() => 0));
$probe('openBlob',        fn() => $pdo->openBlob('t', 'c', 1));

if (defined('PDO::SQLITE_ATTR_OPEN_FLAGS')) {
    $probe('loadExtension', fn() => $pdo->loadExtension('/nonexistent.so'));
}

unset($pdo);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECTF--
REJECTED: setAuthorizer -> PDO_SQLite: setAuthorizer() is not yet supported when PDO::ATTR_POOL_ENABLED is set
REJECTED: openBlob -> PDO_SQLite: openBlob() is not yet supported when PDO::ATTR_POOL_ENABLED is set
%A
Done
