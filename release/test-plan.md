# Test plan: Powergram

Powergram is a network client whose peer is Telegram itself, through
TDLib's MTProto. A fake Telegram is out of reach, so the checks use the real
service without an account: a made-up API key that Telegram rejects.
Never use a real API key or phone number in a release test.

## Fixtures

None. The guest needs internet access (VirtualBox NAT is enough).

The made-up key, written into the guest as the import file:

    api_id=12345
    api_hash=0123456789abcdef0123456789abcdef

## Fresh state

Haiku: `rm -rf ~/config/settings/Powergram ~/config/settings/PaperPlane`
(settings, the API key, TDLib's database, under the app's current and
earlier names). Delete them again after the test. Quit WebPositive.

## Building on haiku-b6-test

Its `/boot` volume is nearly full; `/Data` has room. TDLib is needed at build
time only (static libraries), so extract it instead of installing it:

    mkdir -p /Data/powergram-sdk/tdlib && cd /Data/powergram-sdk
    curl -fsSLO https://eu.hpkg.haiku-os.org/haikuports/r1beta6/x86_64/current/packages/tdlib-0.0.20260718-2-x86_64.hpkg
    package extract -C tdlib tdlib-0.0.20260718-2-x86_64.hpkg
    CXXFLAGS="-I/Data/powergram-sdk/tdlib/develop/headers" \
    LDFLAGS="-L/Data/powergram-sdk/tdlib/develop/lib" TMPDIR=/Data/tmp \
        sh packaging/build-hpkg.sh

## Feature checks

### settings-moved

Steps: with the fresh state, create `~/config/settings/PaperPlane/tdlib/marker`
(the app's settings folder from when it was called Paper Plane), then start
Powergram.

PASS when: `~/config/settings/Powergram/tdlib/marker` exists and
`~/config/settings/PaperPlane` doesn't (the folder, with any login in it,
moved over).

### browser-opens

Steps: the same start (no API key saved).

PASS when: within 10 s a WebPositive process runs with the URL
`https://my.telegram.org/apps` (`ps` shows it). Then quit WebPositive.

### api-key-screen

Steps: the same start, with WebPositive quit.

PASS when: OCR of the screen shows "Telegram API key" (the first page of the
login window), and the app's log is empty.

### telegram-reachable

Steps: quit Powergram; write the made-up key above to
`~/config/settings/Powergram/api_key`; start Powergram. The window shows
"Welcome" with a "Phone number" field (the key was imported and TDLib
started). Type `+999662{p}` (a number in Telegram's old test format; nothing
is sent anywhere but Telegram) and press Enter. The API key page comes back
and opens WebPositive (the first time in this run); quit WebPositive.

PASS when: OCR of the window shows "does not accept this API" (Telegram
answered and rejected the made-up key, and the app showed it on the API key
page), and `~/config/settings/Powergram/api_key` is gone (imported).
