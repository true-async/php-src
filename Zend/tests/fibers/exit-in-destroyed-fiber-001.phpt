--TEST--
exit() in the finally block of a fiber destroyed while suspended ends the script
--FILE--
<?php

register_shutdown_function(function (): void {
    echo "shutdown\n";
});

$fiber = new Fiber(function (): void {
    try {
        Fiber::suspend();
    } finally {
        echo "finally\n";
        exit();
    }
});
$fiber->start();
unset($fiber);

echo "unreachable\n";

?>
--EXPECT--
finally
shutdown
