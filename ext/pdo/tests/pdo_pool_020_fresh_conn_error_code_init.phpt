--TEST--
PDO Pool: fresh pooled connection has error_code initialized to "00000"
--EXTENSIONS--
pdo
pdo_mysql
true_async
--SKIPIF--
<?php
require_once __DIR__ . '/inc/pdo_pool_test.inc';
PDOPoolTest::skip();
?>
--FILE--
<?php
require_once __DIR__ . '/inc/pdo_pool_test.inc';

use function Async\spawn;
use function Async\await;

/*
 * Bug: pdo_pool_factory used ecalloc which zero-fills the struct,
 * leaving conn->error_code as "\0\0\0\0\0\0" instead of "00000".
 * When pdo_pool_sync_error copies this to the template dbh,
 * errorCode() returns NULL instead of "00000".
 *
 * Pool min=0 ensures connections are created on demand (fresh).
 * Multiple concurrent coroutines force multiple fresh connections.
 */

$pdo = PDOPoolTest::poolFactory(poolMax: 5, extra: [
    PDO::ATTR_ERRMODE => PDO::ERRMODE_SILENT,
    PDO::ATTR_POOL_MIN => 0,
]);

$coroutines = [];
for ($i = 0; $i < 5; $i++) {
    $coroutines[] = spawn(function() use ($pdo, $i) {
        // First query on a fresh connection
        $stmt = $pdo->query("SELECT 1 as val");
        $row = $stmt->fetch(PDO::FETCH_ASSOC);

        $code = $pdo->errorCode();
        if ($code === null) {
            echo "Coro {$i}: errorCode is NULL (bug: zero-filled error_code)\n";
        } elseif ($code === '00000') {
            echo "Coro {$i}: OK\n";
        } else {
            echo "Coro {$i}: unexpected errorCode={$code}\n";
        }
    });
}

foreach ($coroutines as $coro) {
    await($coro);
}
echo "Done\n";
?>
--EXPECTF--
Coro %d: OK
Coro %d: OK
Coro %d: OK
Coro %d: OK
Coro %d: OK
Done
