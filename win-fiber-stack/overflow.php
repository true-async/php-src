<?php
// Usage: php -d zend.max_allowed_stack_size=-1 -d fiber.stack_size=256k overflow.php
// Recurses through array_map(), so every level takes C stack, until the fiber's stack ends.
// Expected: the process dies with STATUS_STACK_OVERFLOW (exit code -1073741571, 0xC00000FD), no hang.
function descend(int $depth): array
{
    return array_map('descend', [$depth + 1]);
}

$fiber = new Fiber(static function () {
    descend(0);
});
$fiber->start();
echo "unreachable\n";
