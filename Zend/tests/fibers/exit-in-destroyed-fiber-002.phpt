--TEST--
exit() in the finally block of a fiber destroyed by GC ends the script
--FILE--
<?php

class Holder {
    public ?Holder $self = null;
    public ?Fiber $fiber = null;
}

$holder = new Holder;
$holder->self = $holder;
$holder->fiber = new Fiber(function (): void {
    try {
        Fiber::suspend();
    } finally {
        echo "finally\n";
        exit();
    }
});
$holder->fiber->start();
unset($holder);
gc_collect_cycles();

echo "unreachable\n";

?>
--EXPECT--
finally
