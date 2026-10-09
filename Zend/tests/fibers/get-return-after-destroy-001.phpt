--TEST--
Fiber::getReturn() in a destructor at shutdown, after the suspended fiber was destroyed
--FILE--
<?php

$fiber = new Fiber(function (): void {
    Fiber::suspend();
});
$fiber->start();

// The cycle keeps the holder past the symbol table pass, and the fiber, created first, is destroyed first.
$holder = new class($fiber) {
    public $self;

    public function __construct(public Fiber $fiber) {
        $this->self = $this;
    }

    public function __destruct() {
        try {
            $this->fiber->getReturn();
        } catch (FiberError $error) {
            echo $error->getMessage(), "\n";
        }
    }
};

echo "end\n";

?>
--EXPECT--
end
Cannot get fiber return value: The fiber was destroyed
