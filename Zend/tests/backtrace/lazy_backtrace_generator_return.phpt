--TEST--
Lazy backtrace: exception returned from generator via getReturn
--FILE--
<?php
function inner() { return new Exception("from inner"); }
function gen() {
    return inner();
    yield; // make it a generator
}

$g = gen();
$g->current(); // run to completion
$e = $g->getReturn();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): inner()
#1 [internal function]: gen()
#2 %s(%d): Exception->getTraceAsString()
#3 {main}
