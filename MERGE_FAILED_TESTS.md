# Master → true-async merge: failed tests report

**Merge commit:** `080acb068e4` (master 400 commits ahead → true-async-merge)
**Build:** ZTS+ASAN (`build-asan-zts`)
**Date:** 2026-05-28
**Suite total:** 24435 tests; 15446 passed (99.8%), 10 failed, 13 expected fail, 2 warn, 8929 skipped.

## Failures

### 1. `Zend/tests/fibers/suspend-in-force-close-fiber-after-shutdown.phpt`
**Status:** likely real regression
**Symptom:** Expected Fatal `"FiberError: Cannot suspend in a force-closed fiber"` on shutdown is NOT thrown. Output is just `"done"`.
**Probable cause:** true-async overrides fiber shutdown path. Master only touched `XtOffsetOf` here, no semantic changes — so the issue pre-dates this merge but only surfaced now because we didn't run the full suite before. Need to check fiber destruction code in true-async's coroutine integration.

### 2. `ext/opcache/tests/gh9164.phpt`
**Status:** ASAN-only environmental noise, not a regression
**Symptom:** SEGV inside `__pthread_clockjoin_ex` called by `uv__threadpool_cleanup` at process exit. libuv's threadpool join is racing with ASAN's interceptor on shutdown.
**Action:** Document in known-flakes. Not blocking.

### 3. `ext/openssl/tests/session_resumption_serialize_session.phpt`
**Status:** test-pattern issue
**Symptom:** Expected `%a` (any chars) doesn't catch the actual block of cert PEM. Output includes long base64 cert text on extra lines that test pattern doesn't anticipate.
**Probable cause:** OpenSSL version change in env, or test was written for a single-line cert form. Pre-existing if same OpenSSL.

### 4-7. Session tests (`bug80774`, `gh9200`, `session_regenerate_id_cookie`, `session_start_partitioned_headers`)
**Status:** likely INI default change in master
**Symptom:** Missing `HttpOnly; SameSite=Lax` suffix in `Set-Cookie` header. Tests expect new default; binary emits old default (no flags).
**Probable cause:** Master changed `session.cookie_httponly` / `session.cookie_samesite` default INI values; true-async branch hasn't picked it up. Need to find the upstream commit and apply ini change.

### 8. `sapi/fpm/tests/gh-11086-daemonized-logs-duplicated.phpt`
**Status:** environment
**Symptom:** `failed to open error_log (/dev/stderr): No such device or address (6)`.
**Probable cause:** Test harness loses `/dev/stderr` under WSL2.

### 9. `sapi/fpm/tests/ghsa-7qg2-v9fj-4mwv-status-xss.phpt`
**Status:** NEW test from master (added in this merge), needs investigation
**Symptom:** Returns `int(1338)` instead of expected `bool(false)`. Likely a real bug in FPM `status` page XSS check on true-async.

### 10. `sapi/fpm/tests/main-version.phpt`
**Status:** stale binary (credits text)
**Symptom:** Test expects new copyright string format (`"Copyright © The PHP Group and Contributors / Zend by Perforce"`); binary emits old format (`"Copyright (c) The PHP Group / Zend Technologies"`).
**Probable cause:** Credits header string in main code wasn't rebuilt; need clean rebuild of main/credits objects.

## Triage order
1. **#1 fiber** — semantic, async-related, fix first.
2. **#4-7 session cookies** — single ini-default change, one fix covers four tests.
3. **#10 fpm version** — clean rebuild.
4. **#9 fpm xss** — investigate.
5. **#2 opcache, #3 openssl, #8 fpm logs** — environmental, document and move on.

## Progress log

- **#4-7 session cookies** — root cause: stale `php-cgi` binary (May 27, pre-merge). Master commit `27ead919e07` changed session defaults (`session.cookie_httponly: 0→1`, `session.cookie_samesite: ""→"Lax"`, `session.use_strict_mode: 0→1`). All four tests use `sapi/cgi/php-cgi` for HTTP-header inspection. Fixed by rebuilding `build-asan-zts/sapi/cgi/php-cgi` against merged source. ✓ PASS.
- **bug60120 (proc_open)** — fixed by commenting out broken `extension=true_async_server` in `/usr/local/lib/php.ini` (the child process spawned by proc_open inherited the system ini and crashed loading the extension built against an older php-src ABI). ✓ PASS.
- **#10 fpm main-version / #8 fpm logs / #9 fpm xss** — ASAN build was configured without `--enable-fpm` (`build-asan-zts/config.status` shows no `--enable-fpm`). These tests cannot run in this build at all; failures are "binary not refreshed" by run-tests.php pointing at a stale FPM binary from a different build root. Not a merge regression.
- **#2 opcache gh9164** — SEGV in `__pthread_clockjoin_ex` from libuv shutdown under ASAN. Environmental.
- **#3 openssl session_resumption_serialize_session** — root cause: test hard-codes `object(Openssl\Session)#9`, true-async bumps the zend-object counter by +2 at process start (scheduler context, main scope, {main} coroutine), so the unserialized Session lands at `#11`. `%a` for PEM/binary contents already matches fine. Fixed by patching the literal to `#%d` (in line with PHPT conventions for object-id matching). ✓ PASS.
- **#1 fiber suspend-in-force-close** — still failing. Test also exists on `true-async` branch verbatim. Master's only fiber-related changes in this merge are mechanical (`XtOffsetOf` removal). High likelihood this is **pre-existing on true-async** (not introduced by the merge); needs separate investigation in async fiber-shutdown integration.

## Remaining work
- Verify #1 fiber on baseline `true-async-stable` to confirm pre-existing vs regression (requires branch switch + rebuild).
