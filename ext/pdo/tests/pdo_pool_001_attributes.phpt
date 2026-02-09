--TEST--
PDO Pool: Pool attribute constants exist
--EXTENSIONS--
pdo
true_async
--FILE--
<?php
// Test that pool attribute constants are defined
echo "PDO::ATTR_POOL_ENABLED = " . PDO::ATTR_POOL_ENABLED . "\n";
echo "PDO::ATTR_POOL_MIN = " . PDO::ATTR_POOL_MIN . "\n";
echo "PDO::ATTR_POOL_MAX = " . PDO::ATTR_POOL_MAX . "\n";
echo "PDO::ATTR_POOL_HEALTHCHECK_INTERVAL = " . PDO::ATTR_POOL_HEALTHCHECK_INTERVAL . "\n";

// Verify they are sequential
$enabled = PDO::ATTR_POOL_ENABLED;
$min = PDO::ATTR_POOL_MIN;
$max = PDO::ATTR_POOL_MAX;
$healthcheck = PDO::ATTR_POOL_HEALTHCHECK_INTERVAL;

if ($min == $enabled + 1 && $max == $min + 1 && $healthcheck == $max + 1) {
    echo "Constants are sequential: OK\n";
} else {
    echo "Constants are NOT sequential: FAIL\n";
}

echo "Done\n";
?>
--EXPECTF--
PDO::ATTR_POOL_ENABLED = %d
PDO::ATTR_POOL_MIN = %d
PDO::ATTR_POOL_MAX = %d
PDO::ATTR_POOL_HEALTHCHECK_INTERVAL = %d
Constants are sequential: OK
Done
