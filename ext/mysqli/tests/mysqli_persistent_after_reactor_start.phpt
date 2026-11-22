--TEST--
mysqli: a persistent link used after the reactor started
--EXTENSIONS--
mysqli
true_async
--SKIPIF--
<?php
require_once 'skipifconnectfailure.inc';
?>
--INI--
mysqli.allow_persistent=1
mysqli.max_persistent=-1
--FILE--
<?php
require_once 'connect.inc';

use function Async\spawn;
use function Async\await;

function plink(): mysqli {
    global $host, $user, $passwd, $db, $port, $socket;
    return new mysqli("p:$host", $user, $passwd, $db, $port, $socket);
}

$rows = "SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3";

// Released while the main coroutine runs, with a transaction open.
function scoped(): void {
    $link = plink();
    $link->begin_transaction();
    file(__FILE__); // starts the reactor
}
scoped();
echo "scoped link released\n";

// An unbuffered result freed across the reactor.
$link = plink();
$result = $link->query($rows, MYSQLI_USE_RESULT);
echo "first row: ", $result->fetch_row()[0], "\n";
file(__FILE__);
$result = null;

// The same link picked up again in one request.
$id = $link->thread_id;
$link = null;
echo "picked up again: ", var_export(plink()->thread_id === $id, true), "\n";

// Opened and used inside a coroutine.
echo "coroutine: ", await(spawn(function () {
    $stmt = plink()->prepare("SELECT ? + 1");
    $v = 5;
    $stmt->bind_param("i", $v);
    $stmt->execute();
    return $stmt->get_result()->fetch_row()[0];
})), "\n";
echo "done\n";
?>
--EXPECT--
scoped link released
first row: 1
picked up again: true
coroutine: 6
done
