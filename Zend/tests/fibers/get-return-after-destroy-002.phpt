--TEST--
Fiber::getReturn() of a fiber destroyed while suspended and kept by a destructor in GC
--FILE--
<?php

class Holder {
    public ?Holder $self = null;
    public ?Fiber $fiber = null;

    public function __destruct() {
        global $kept;
        $kept = $this->fiber;
    }
}

$holder = new Holder;
$holder->self = $holder;
$holder->fiber = new Fiber(function (): void {
    Fiber::suspend();
});
$holder->fiber->start();
unset($holder);
gc_collect_cycles();

var_dump($kept->isTerminated());

try {
    $kept->getReturn();
} catch (FiberError $error) {
    echo $error->getMessage(), "\n";
}

?>
--EXPECT--
bool(true)
Cannot get fiber return value: The fiber was destroyed
