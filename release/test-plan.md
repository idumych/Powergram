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

Haiku: `rm -rf ~/config/settings/Powergram` (settings, the API key, TDLib's
database). Delete it again after the test.

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

### api-key-screen

Steps: with the fresh state, start Powergram.

PASS when: OCR of the screen shows "Telegram API key" (the first page of the
login window), and the app's log is empty.

### telegram-reachable

Steps: quit Powergram; write the made-up key above to
`~/config/settings/Powergram/api_key`; start Powergram. The window shows
"Welcome" with a "Phone number" field (the key was imported and TDLib
started). Type `+999662{p}` (a number in Telegram's old test format; nothing
is sent anywhere but Telegram) and press Enter.

PASS when: OCR of the window shows "does not accept this API" (Telegram
answered and rejected the made-up key, and the app showed it on the API key
page), and `~/config/settings/Powergram/api_key` is gone (imported).
