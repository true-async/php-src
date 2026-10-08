--TEST--
A stream read that throws does not lose data already read
--FILE--
<?php
class ChunkStream {
    public $context;
    public static array $chunks;
    private array $left;

    function stream_open($path, $mode, $options, &$opened_path) {
        $this->left = self::$chunks;
        return true;
    }

    function stream_read($count) {
        $chunk = array_shift($this->left);
        if ($chunk instanceof Exception) {
            throw $chunk;
        }
        return $chunk ?? '';
    }

    function stream_eof() {
        return $this->left === [];
    }

    function stream_stat() {
        return [];
    }
}

stream_wrapper_register('chunks', 'ChunkStream');

function test(string $name, callable $read, ?string $filter = null) {
    ChunkStream::$chunks = ["line1\nli", "ne2\nlin", new Exception("interrupted"), "e3\n"];
    $stream = fopen('chunks://', 'r');
    if ($filter !== null) {
        stream_filter_append($stream, $filter, STREAM_FILTER_READ);
    }
    $data = '';
    $thrown = 0;
    while (!feof($stream)) {
        try {
            $data .= $read($stream);
        } catch (Exception) {
            $thrown++;
        }
    }
    echo $name, $filter !== null ? " with $filter" : '', ": thrown $thrown, at ", ftell($stream), ", ", json_encode($data), "\n";
}

foreach ([null, 'string.toupper'] as $filter) {
    test('fread', fn($stream) => fread($stream, 7), $filter);
    test('fread of a chunk', fn($stream) => fread($stream, 8192), $filter);
    test('fgets', fn($stream) => fgets($stream), $filter);
    test('fgets with a length', fn($stream) => fgets($stream, 100), $filter);
    test('stream_get_contents', fn($stream) => stream_get_contents($stream), $filter);
    test('stream_get_contents with a length', fn($stream) => stream_get_contents($stream, 100), $filter);
    test('stream_get_line', fn($stream) => stream_get_line($stream, 100, "\n") . "\n", $filter);
}

// With a chunk size of 1 a read takes what is buffered, then asks the wrapper directly
ChunkStream::$chunks = ["line1\nli", new Exception("interrupted"), "ne2\nline3\n"];
$stream = fopen('chunks://', 'r');
$data = fread($stream, 4);
stream_set_chunk_size($stream, 1);
try {
    fread($stream, 100);
} catch (Exception $e) {
    echo "direct: ", $e->getMessage(), "\n";
}
while (!feof($stream)) {
    $data .= fread($stream, 100);
}
echo "direct: at ", ftell($stream), ", ", json_encode($data), "\n";
?>
--EXPECT--
fread: thrown 1, at 18, "line1\nline2\nline3\n"
fread of a chunk: thrown 1, at 18, "line1\nline2\nline3\n"
fgets: thrown 1, at 18, "line1\nline2\nline3\n"
fgets with a length: thrown 1, at 18, "line1\nline2\nline3\n"
stream_get_contents: thrown 1, at 18, "line1\nline2\nline3\n"
stream_get_contents with a length: thrown 1, at 18, "line1\nline2\nline3\n"
stream_get_line: thrown 1, at 18, "line1\nline2\nline3\n"
fread with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
fread of a chunk with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
fgets with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
fgets with a length with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
stream_get_contents with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
stream_get_contents with a length with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
stream_get_line with string.toupper: thrown 1, at 18, "LINE1\nLINE2\nLINE3\n"
direct: interrupted
direct: at 18, "line1\nline2\nline3\n"
