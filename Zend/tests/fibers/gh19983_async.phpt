--TEST--
GH-19983 (GC Assertion Failure with fibers, generators and destructors): TrueAsync collects the suspended fibers
--SKIPIF--
<?php
if (!extension_loaded("true_async")) die("skip TrueAsync runtime required");
?>
--INI--
memory_limit=16M
--FILE--
<?php
class a
{
    public static int $calls = 0;

    function __destruct()
    {
        if (++self::$calls === 20000) {
            echo memory_get_usage() < 12 * 1024 * 1024 ? "OK\n" : "Memory grew to " . memory_get_usage() . "\n";
            exit;
        }

        $gen = (function () {
            $from = (function () {
                $cv = [new a];
                Fiber::suspend();
            })();
            yield from $from;
        })();
        $fiber = new Fiber(function () use ($gen, &$fiber) {
            $gen->current();
        });
        $fiber->start();
    }
}
new a;
?>
--EXPECT--
OK
