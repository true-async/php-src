--TEST--
mysqli: a persistent link rolled back on release after the reactor started
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
mysqli.rollback_on_cached_plink=1
--FILE--
<?php
require_once 'connect.inc';

// The rollback runs after mysqlnd ended the persistent session, so it does I/O
// on a stream whose per-request state was already given up once.
function scoped(): void {
    global $host, $user, $passwd, $db, $port, $socket;
    $link = new mysqli("p:$host", $user, $passwd, $db, $port, $socket);
    $link->begin_transaction();
    file(__FILE__); // starts the reactor
}
scoped();
echo "done\n";
?>
--EXPECT--
done
