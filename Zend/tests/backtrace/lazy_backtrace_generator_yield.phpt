--TEST--
Lazy backtrace: exception yielded from generator, trace read while generator suspended
--FILE--
<?php
function gen() {
    yield new Exception("from gen");
}

$g = gen();
$e = $g->current();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 [internal function]: gen()
#1 %s(%d): Generator->current()
#2 {main}
