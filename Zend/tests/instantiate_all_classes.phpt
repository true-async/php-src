--TEST--
Try to instantiate all classes without arguments
--FILE--
<?php

foreach (get_declared_classes() as $class) {
    try {
        if ($class === 'Async\FutureState') {
            $state = new $class;
            (new \Async\Future($state))->ignore();
        } elseif ($class === 'Async\Future') {
            continue;
        } else {
            new $class;
        }
    } catch (Throwable) {}
}

?>
===DONE===
--EXPECT--
===DONE===
