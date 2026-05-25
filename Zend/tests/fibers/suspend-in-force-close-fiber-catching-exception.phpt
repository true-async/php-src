--TEST--
Suspend in force-closed fiber, catching exception thrown from destructor
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php

try {
    (function (): void {
        $fiber = new Fiber(function (): void {
            try {
                Fiber::suspend();
            } finally {
                Fiber::suspend();
            }
        });

        $fiber->start();
    })();
} catch (FiberError $exception) {
    echo $exception->getMessage(), "\n";
}

echo "done\n";

?>
--EXPECTF--
done

Fatal error: Uncaught FiberError: Cannot suspend in a force-closed fiber in %s:%d
Stack trace:
#0 %s(%d): Fiber::suspend()
#1 [internal function]: {closure:%s:%d}()
#2 {main}
  thrown in %s on line %d
