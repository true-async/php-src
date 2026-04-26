--TEST--
PDO_SQLite Pool: in-memory DSNs are rejected
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$cases = [
    'sqlite::memory:',
    'sqlite:',                                   // empty after prefix → private temp
    'sqlite:file::memory:',                      // URI memory, no cache=shared
    'sqlite:file:?mode=memory',                  // URI mode=memory, no cache=shared
    'sqlite:file:foo?mode=memory',               // named in-memory, no sharing
    'sqlite:file::memory:?mode=memory',          // both, still no cache=shared
];

foreach ($cases as $dsn) {
    try {
        new PDO($dsn, null, null, [
            PDO::ATTR_POOL_ENABLED => true,
            PDO::ATTR_POOL_MIN => 1,
            PDO::ATTR_POOL_MAX => 2,
        ]);
        echo "UNEXPECTED OK: $dsn\n";
    } catch (PDOException $e) {
        echo "REJECTED: $dsn -> ", strstr($e->getMessage(), 'in-memory'), "\n";
    }
}

// Sanity: shared in-memory variants are accepted
$accepted = [
    'sqlite:file::memory:?cache=shared',
    'sqlite:file:foo?mode=memory&cache=shared',
    'sqlite:file:bar?cache=shared&mode=memory',
];

foreach ($accepted as $dsn) {
    try {
        $pdo = new PDO($dsn, null, null, [
            PDO::ATTR_POOL_ENABLED => true,
            PDO::ATTR_POOL_MIN => 1,
            PDO::ATTR_POOL_MAX => 2,
        ]);
        echo "ACCEPTED: $dsn\n";
        unset($pdo);
    } catch (PDOException $e) {
        echo "UNEXPECTED REJECT: $dsn -> ", $e->getMessage(), "\n";
    }
}

echo "Done\n";
?>
--EXPECTF--
REJECTED: sqlite::memory: -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
REJECTED: sqlite: -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
REJECTED: sqlite:file::memory: -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
REJECTED: sqlite:file:?mode=memory -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
REJECTED: sqlite:file:foo?mode=memory -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
REJECTED: sqlite:file::memory:?mode=memory -> in-memory databases cannot be used with PDO::ATTR_POOL_ENABLED%a
ACCEPTED: sqlite:file::memory:?cache=shared
ACCEPTED: sqlite:file:foo?mode=memory&cache=shared
ACCEPTED: sqlite:file:bar?cache=shared&mode=memory
Done
