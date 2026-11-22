--TEST--
PDO_SQLite Pool: a PDO whose pool setup failed in the constructor stays unusable when the failure is caught
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');

// A subclass can catch what parent::__construct() threw and carry on with the
// object; it must then be refused like any PDO whose constructor failed.
class CatchingPdo extends PDO {
    public function __construct(string $dsn, array $options) {
        try {
            parent::__construct($dsn, null, null, $options);
        } catch (Throwable $e) {
            echo "constructor: ", get_class($e), "\n";
        }
    }
}

$cases = [
    'malformed init SQL' => [
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_INIT_SQL => '',
    ],
    'pre-warm fails' => [
        PDO::ATTR_POOL_ENABLED => true,
        PDO::ATTR_POOL_MIN => 1,
        PDO::ATTR_POOL_INIT_SQL => ['SELECT no_such_function()'],
    ],
];

foreach ($cases as $name => $options) {
    echo "$name:\n";
    $pdo = new CatchingPdo("sqlite:$dbfile", $options);
    foreach ([
        'exec' => fn () => $pdo->exec('SELECT 1'),
        'getAttribute' => fn () => $pdo->getAttribute(PDO::ATTR_SERVER_VERSION),
        'setAttribute' => fn () => $pdo->setAttribute(PDO::ATTR_TIMEOUT, 5),
    ] as $call => $fn) {
        try {
            $fn();
            echo "  $call: no exception\n";
        } catch (Throwable $e) {
            echo "  $call: ", get_class($e), ": ", $e->getMessage(), "\n";
        }
    }
    unset($pdo);
}

@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
malformed init SQL:
constructor: ValueError
  exec: Error: CatchingPdo object is uninitialized
  getAttribute: Error: CatchingPdo object is uninitialized
  setAttribute: Error: CatchingPdo object is uninitialized
pre-warm fails:
constructor: PDOException
  exec: Error: CatchingPdo object is uninitialized
  getAttribute: Error: CatchingPdo object is uninitialized
  setAttribute: Error: CatchingPdo object is uninitialized
Done
