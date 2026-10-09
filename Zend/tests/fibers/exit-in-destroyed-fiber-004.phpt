--TEST--
exit() in the finally block of a fiber destroyed with a pending return value skips the outer finally
--FILE--
<?php

function make(): Fiber {
    $fiber = new Fiber(function (): void {
        try {
            Fiber::suspend();
        } finally {
            echo "finally\n";
            exit();
        }
    });
    $fiber->start();

    return $fiber;
}

function run(): Fiber {
    try {
        try {
            return make();
        } finally {
            throw new Exception('releases the pending return value');
        }
    } finally {
        echo "unreachable\n";
    }
}

run();

?>
--EXPECT--
finally
