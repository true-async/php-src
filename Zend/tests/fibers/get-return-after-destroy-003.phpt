--TEST--
Fiber::getReturn() of a fiber that keeps itself alive while it is destroyed
--FILE--
<?php

$fiber = new Fiber(function (): void {
    try {
        Fiber::suspend();
    } finally {
        $GLOBALS['kept'] = Fiber::getCurrent();
    }
});
$fiber->start();
unset($fiber);

try {
    $kept->getReturn();
} catch (FiberError $error) {
    echo $error->getMessage(), "\n";
}

?>
--EXPECT--
Cannot get fiber return value: The fiber was destroyed
