# Community submission draft

## Short description

CK42X T-Embed PassVault is an open-source LilyGO T-Embed / T-Embed CC1101 firmware that provides a PIN-gated encrypted password vault, generated passwords, and explicit USB HID password typing.

## Safe wording

This is a small field utility for test credentials, labs, demos, and controlled personal workflows. It is not a hardened/audited password manager.

## Links

- Source: https://github.com/lordbuffcloud/ck42x-t-embed-passvault
- Original Flipper app inspiration: https://github.com/lordbuffcloud/flipper-ck42x-passvault
- CK42X: https://ck42x.com

## Suggested post

I built a CK42X PassVault firmware for the LilyGO T-Embed / T-Embed CC1101.

It is a standalone ESP32-S3 firmware with:

- PIN-gated local vault
- AES-GCM encrypted storage in NVS
- generated password presets
- USB serial provisioning shell
- T-Embed screen/encoder selection
- explicit USB HID password typing only after confirmation

This is early MVP/community-test firmware, not an audited password manager. I would appreciate hardware feedback from other T-Embed owners before making stronger security claims.

## Community targets checked

- Bruce App Store: useful for BruceJS scripts/themes, but this is standalone replacement firmware, not a BruceJS app.
- LILYGO community/forum: appropriate for a repo/release announcement.
- GitHub Releases: appropriate primary distribution surface.
