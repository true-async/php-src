--TEST--
A fatal error in a destructor after shutdown moved into a coroutine leaves the remaining destructors uncalled
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php
class Remaining {
    public function __destruct() {
        echo "Remaining::__destruct\n";
    }
}

class Throwing {
    public function __destruct() {
        throw new Exception('Throwing::__destruct');
    }
}

class Suspending {
    public function __destruct() {
        echo "Suspending::__destruct\n";
        Async\delay(1);
    }
}

$remaining = new Remaining;
$throwing = new Throwing;
$suspending = new Suspending;
echo "Done\n";
?>
--EXPECTF--
Done
Suspending::__destruct

Fatal error: Uncaught Exception: Throwing::__destruct in %s:%d
Stack trace:
#0 [internal function]: Throwing->__destruct()
#1 {main}
  thrown in %s on line %d
