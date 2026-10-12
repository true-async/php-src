--TEST--
Io\Poll\SignalHandle: a blocked signal unblocked while two handles watch it stays blocked until the last removal
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
?>
--FILE--
<?php
// SIGCHLD only because the call refuses an empty list; a SIG_SETMASK naming
// the watched signal would take the unblock back, so the probe only blocks
// SIGCHLD and unblocks it again if it was not blocked before
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    if (!in_array(SIGCHLD, $mask, true)) {
        pcntl_sigprocmask(SIG_UNBLOCK, [SIGCHLD]);
    }
    return in_array($signo, $mask, true);
}

$ctx = new Io\Poll\Context();
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$first = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
$second = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR1]);
$first->remove();
var_dump(is_blocked(SIGUSR1));
$second->remove();
var_dump(is_blocked(SIGUSR1));
?>
--EXPECT--
bool(true)
bool(false)
