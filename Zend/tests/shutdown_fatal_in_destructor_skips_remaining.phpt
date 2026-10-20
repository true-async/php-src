--TEST--
A fatal error in a destructor during shutdown leaves the remaining destructors uncalled
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

$remaining = new Remaining;
$throwing = new Throwing;
echo "Done\n";
?>
--EXPECTF--
Done

Fatal error: Uncaught Exception: Throwing::__destruct in %s:%d
Stack trace:
#0 [internal function]: Throwing->__destruct()
#1 {main}
  thrown in %s on line %d
