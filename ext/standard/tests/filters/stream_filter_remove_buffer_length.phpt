--TEST--
Flushing a read filter keeps the read buffer length in step with the buffer
--FILE--
<?php

/* The filter holds everything it is fed and releases it in one bucket when the
 * chain is flushed, so the flush has to grow the stream read buffer. */
class Hoarder extends php_user_filter
{
    private string $held = '';

    public function filter($in, $out, &$consumed, $closing): int
    {
        while ($bucket = stream_bucket_make_writeable($in)) {
            $consumed += $bucket->datalen;
            $this->held .= $bucket->data;
        }

        if ($closing) {
            if ($this->held === '') {
                return PSFS_FEED_ME;
            }
            stream_bucket_append($out, stream_bucket_new($this->stream, $this->held));
            $this->held = '';
            return PSFS_PASS_ON;
        }

        if (strlen($this->held) > 1) {
            stream_bucket_append($out, stream_bucket_new($this->stream, $this->held[0]));
            $this->held = substr($this->held, 1);
            return PSFS_PASS_ON;
        }

        return PSFS_FEED_ME;
    }
}

stream_filter_register('hoarder', 'Hoarder');

const SIZE = 3200000;

$tmpfile = tempnam(sys_get_temp_dir(), 'filter_buflen_');
file_put_contents($tmpfile, str_repeat('0123456789abcdef', SIZE / 16));

$handle = fopen($tmpfile, 'r');
$filter = stream_filter_append($handle, 'hoarder', STREAM_FILTER_READ);

$taken = 0;
for ($i = 0; $i < 12; $i++) {
    $taken += strlen(fread($handle, 1));
}

var_dump(stream_filter_remove($filter));

$rest = 0;
while (true) {
    $chunk = fread($handle, 4096);
    if ($chunk === false || $chunk === '') {
        break;
    }
    $rest += strlen($chunk);
}

printf("delivered: %d of %d\n", $taken + $rest, SIZE);
printf("eof: %s\n", var_export(feof($handle), true));

fclose($handle);

?>
--CLEAN--
<?php
foreach (glob(sys_get_temp_dir() . '/filter_buflen_*') as $leftover) {
    unlink($leftover);
}
?>
--EXPECT--
bool(true)
delivered: 3200000 of 3200000
eof: true
