--TEST--
exit() in the finally block of a fiber destroyed while an exception unwinds its owner
--FILE--
<?php

function run(): void {
    $fiber = new Fiber(function (): void {
        try {
            Fiber::suspend();
        } finally {
            echo "finally\n";
            exit();
        }
    });
    $fiber->start();

    throw new Exception('unwinds the fiber owner');
}

try {
    run();
} catch (Exception $exception) {
    echo "unreachable\n";
}

?>
--EXPECT--
finally
