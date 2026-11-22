--TEST--
PDO_SQLite Pool: ATTR_POOL_INIT_SQL is validated in the constructor and is construction-only
--EXTENSIONS--
pdo
pdo_sqlite
true_async
--FILE--
<?php

$dbfile = tempnam(sys_get_temp_dir(), 'pdo_sqlite_pool_');
$dsn = "sqlite:$dbfile";

$cases = [
    'int'            => 42,
    'null'           => null,
    'string keys'    => ['a' => 'PRAGMA foreign_keys = ON'],
    'not a list'     => [1 => 'PRAGMA foreign_keys = ON'],
    'empty string'   => '',
    'empty element'  => ['PRAGMA foreign_keys = ON', ''],
    'int element'    => ['PRAGMA foreign_keys = ON', 7],
    'array element'  => [['PRAGMA foreign_keys = ON']],
];

foreach ($cases as $name => $value) {
    try {
        new PDO($dsn, null, null, [
            PDO::ATTR_POOL_ENABLED => true,
            PDO::ATTR_POOL_INIT_SQL => $value,
        ]);
        echo "$name: accepted\n";
    } catch (Throwable $e) {
        echo "$name: ", get_class($e), ': ', $e->getMessage(), "\n";
    }
}

// A reference must not slip a non-string past the check.
$s = 'PRAGMA foreign_keys = ON';
$list = [&$s];
$pdo = new PDO($dsn, null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => $list,
]);
$s = 123;
var_dump($pdo->getAttribute(PDO::ATTR_POOL_INIT_SQL));
echo "fk: ", $pdo->query('PRAGMA foreign_keys')->fetchColumn(), "\n";

// An empty list configures nothing.
$empty = new PDO($dsn, null, null, [
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => [],
]);
var_dump($empty->getAttribute(PDO::ATTR_POOL_INIT_SQL));

// Changing it later would leave existing slots on the old baseline.
$silent = new PDO($dsn, null, null, [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
    PDO::ATTR_POOL_ENABLED => true,
    PDO::ATTR_POOL_INIT_SQL => ['PRAGMA foreign_keys = ON'],
]);
var_dump($silent->setAttribute(PDO::ATTR_POOL_INIT_SQL, ['PRAGMA foreign_keys = OFF']));
echo "error: ", $silent->errorCode(), "\n";
// Rejected without building a connection.
echo "slots: ", $silent->getPool()->count(), "\n";

unset($pdo, $empty, $silent);
@unlink($dbfile);

echo "Done\n";
?>
--EXPECT--
int: TypeError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL must be of type array|string, int given
null: TypeError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL must be of type array|string, null given
string keys: ValueError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL must be a list
not a list: ValueError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL must be a list
empty string: ValueError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL element 0 must not be empty
empty element: ValueError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL element 1 must not be empty
int element: TypeError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL element 1 must be of type string, int given
array element: TypeError: PDO::__construct(): Argument #4 ($options) PDO::ATTR_POOL_INIT_SQL element 0 must be of type string, array given
array(1) {
  [0]=>
  string(24) "PRAGMA foreign_keys = ON"
}
fk: 1
array(0) {
}
bool(false)
error: IM001
slots: 0
Done
