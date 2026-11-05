#!/bin/bash
# Builds the Android release bundle for Google Play, signed with the upload key.
#
# The key and its password stay outside the repo, in ~/.midimorf (or
# $MIDIMORF_SIGNING_DIR). The first run creates them: back that folder up.
# Projucer's own signing settings would put the password in the .jucer, so the
# generated project stays debug-signed and this script signs at build time.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="$root/MidiMorfApp"
signing="${MIDIMORF_SIGNING_DIR:-$HOME/.midimorf}"
keystore="$signing/android-upload.p12"
passfile="$signing/android-upload.pass"
alias=upload

export JAVA_HOME="${JAVA_HOME:-/Applications/Android Studio.app/Contents/jbr/Contents/Home}"
keytool="$JAVA_HOME/bin/keytool"

if [[ ! -f "$keystore" ]]; then
    (umask 077 && mkdir -p "$signing" && openssl rand -hex 24 > "$passfile")
    MIDIMORF_KEY_PASS="$(cat "$passfile")" "$keytool" -genkeypair -alias "$alias" \
        -keyalg RSA -keysize 4096 -validity 10000 -storetype PKCS12 \
        -keystore "$keystore" -storepass:env MIDIMORF_KEY_PASS \
        -dname "CN=Retro Relic, O=Retro Relic" 2>/dev/null
    chmod 600 "$keystore"
    echo "Created a new upload key in $signing. Back that folder up."
fi

password="$(cat "$passfile")"

"/Applications/JUCE/Projucer.app/Contents/MacOS/Projucer" --resave "$app/MidiMorfApp.jucer"

# Passed through the environment so the password stays out of the process list.
cd "$app/Builds/Android"
env "ORG_GRADLE_PROJECT_android.injected.signing.store.file=$keystore" \
    "ORG_GRADLE_PROJECT_android.injected.signing.store.password=$password" \
    "ORG_GRADLE_PROJECT_android.injected.signing.key.alias=$alias" \
    "ORG_GRADLE_PROJECT_android.injected.signing.key.password=$password" \
    ./gradlew --no-daemon clean bundleRelease_Release

bundle="$(find app/build/outputs/bundle/release_Release -name '*.aab' | head -1)"

# Play rejects a debug-signed bundle, and this catches that here instead.
fingerprint() { grep -E '^[[:space:]]*SHA256:' | head -1 | awk '{print $2}'; }
expected="$(MIDIMORF_KEY_PASS="$password" "$keytool" -list -v -alias "$alias" -keystore "$keystore" \
    -storepass:env MIDIMORF_KEY_PASS | fingerprint)"
actual="$("$keytool" -printcert -jarfile "$bundle" | fingerprint)"

if [[ -z "$expected" || "$actual" != "$expected" ]]; then
    echo "The bundle is not signed with the upload key: $bundle" >&2
    exit 1
fi

echo "Signed with the upload key ($actual)"
echo "$PWD/$bundle"
