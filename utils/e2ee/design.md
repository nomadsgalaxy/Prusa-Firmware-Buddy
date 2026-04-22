# Robust bgcodes

## Features we would like

* End-2-end encryption ‒ the gcodes travel through different places (Connect,
  over Link, users carry it on USB sticks around), it would be good if they were
  sure the gcode doesn't fall in the wrong hands.
  - We _probably_ want to leave previews and metadata unencrypted ‒ to
    explicitly allow Connect show the previews, etc.
* Security at rest ‒ stealing a USB disk from the printer should not be enough
  to read the model / gcode (one would need to steal the whole printer for
  that).
* Integrity ‒ the file wasn't changed on the way / corrupted.
* Authenticity ‒ originates from trusted party.

We don't necessarily need all of them with each gcode, but the users could
mix-and-match them as needed.

We do want to keep all the existing nice features we have ‒ streaming support,
power panic, etc.

The printer is not powerful; we can afford RSA operations before the print, but
not during. Symmetric crypto and hashing during print is OK.

## Proposal

### Keys

* A printer has its own RSA key pair. The private one is kept inside the printer
  (in XFlash, the firmware won't willingly export this one), the public is
  shared with the world ‒ possibly by export to the USB stick or through
  Connect.
* Slicer can mark a key as belonging to a printer (maybe by checking the
  fingerprint on its own screen and on the printer).
* The purpose of this key pair is to read gcodes encrypted for this specific
  printer.
* Similarly, a slicer can generate its own RSA keypair, public one can be put
  into a printer and marked as „trusted“.
* This pair is used for signing the gcode. It also can be used for
  self-encrypting the gcode, so the slicer that generated some encrypted gcode
  can still read it and show the moves (or the slicer can have another key-pair
  for that, basically pretending it's another printer).
* We support gcodes signed by single slicer, but encrypted for multiple
  recipient printers.

### BGcode blocks

* In the bgcode structure, we introduce three new block types. They live in
  between the metadata+previews section and the gcode section.
  - First reason is, introduction of new types makes the rest of the file
    unreadable for previous versions, so we want keep whatever we don't encrypt
    before them.
  - Furthermore, we want these blocks to introduce the relevant keys for
    following encryption and contain hashes of the unencrypted parts, so this is
    a natural place.
* One of them is the identity block, which holds the creator's identity. This
  one is used for integrity and authenticity checks.
* The second new type is a key block. It's purpose is to hold an encrypted
  one-time symmetric key for the actual encryption and signing of individual
  gcode blocks. This can be present multiple times to allow multiple printers to
  print the same bgcode, each printer picks its own.
* The rest of the file is a sequence of encrypted blocks, each signed and
  encrypted separately (to allow streaming, power-panic and similar ‒ we need to
  be able to start reading somewhere in the middle and that is the reason the
  file is split into blocks in the first place). Each encrypted block holds
  another block inside itself ‒ usually, the inner block is gcode block.
  Technically, we _could_ support encrypting metadata or thumbnails this way, we
  just don't want to implement it in the first version.
* There's a protection against block reordering, truncation, etc ‒ see below.
* If we want to not have certain feature, we „stub“ it out, to make the format
  and handling easier ‒ otherwise we would have to define how exactly it looks
  if we have encryption but not signing (but want _some_ kind of integrity),
  etc…:
  - If we don't want authenticity, we just use a one-time identity.
  - If we don't want encryption, we still encrypt, but leave the symmetric key
    in open ‒ there's a key block but it is not RSA encrypted.

#### The identity block

The identity block contains following things:

* The slicer's public key (signing key ‒ for the purpose of checking it was
  signed with _something_ even though we don't have it in our store).
* Human readable description of the identity (eg. name that can be shown on the
  display).
* Hash of the intro section (all the blocks before this one, for checking
  integrity / authenticity).
* Hash of all the key blocks (just following below, similar).

The whole block is then signed by the slicer's key.

#### Key block

There are potentially multiple key blocks (we want to allow one gcode printable
by multiple designated printers), at least one. The payload of each is a pair of
one-time symmetric keys (currently AES-128, must not be reused through multiple
gcodes), one for encryption, one for signing. The encryption keys is the same
for all the recipient printers, each printer has a different signing key.

The payload is either unencrypted (in case we want just integrity /
authenticity, but not encryption), or encrypted with RSA.

RSA encryption also adds a fingerprint of both involved public keys (slicer and
the printer for which this key block is). See the following for the reason for
this addition.

<https://theworld.com/~dtd/sign_encrypt/sign_encrypt7.html>.

It is allowed for the slicer to include its own key block ‒ that is, encrypted
for its own private key (it may be the same or different RSA keypair as its
identity), which would allow it to later decode the gcode and visualize it.

Note:

The trick with a separate verification key for each printer is to prevent one
printer being able to forge data for another printer (if they shared the same
key). We could do it by RSA-signing each gcode block, but we need to avoid
expensive operations during the print.

A printer tries to find either a block encrypted by its own key, or an
unencrypted one.

#### The encrypted blocks

After the key blocks, there's a sequence of symmetrically encrypted blocks.

The payload of the block is another embedded block including its header (that
is, we „hide“ the real header in the encryption too). For now, we implement just
gcode blocks inside, but in the future it may be possible to add eg. encrypted
thumbnails.

Furthermore, a parameter specifies if this block is the last one of the file.
This is to prevent truncation by an adversary.

Each gcode block is encrypted separately by AES-128-CBC with the one-time key
(as extracted from the key block) and IV being equal to the byte offset of the
block inside the file (therefore, each block gets a unique IV).

It is verified with HMAC with the key for each printer. That is, a block has
multiple HMAC verification tags, one corresponding to each key block. They are
stored in the same order ‒ a printer that finds its own key in nth key block
uses the nth HMAC tag.

The block headers (unencrypted) and the file starting position (not _stored_ in
the file, but virtually part of the data) is included in the signature ‒ to
prevent reordering.

This is Encrypt-Then-Mac.

All this is „inside“ the block payload as viewed as the libbgcode format. The
block still can have (according to the whole-file headers) its CRC; this could
be used to check the file against accidental damage even by a party that doesn't
have the keys.

## Should assure

* The header part is not tampered with ‒ signed through the identity block.
* The file can't be truncated ‒ the last block must be with the flag set and
  signed.
* Keys can't be tampered with ‒ signed into the identity block.
* Gcode block can't be tampered with ‒ verified with a HMAC, key is signed
  inside the key block.
* Non-recipient can't read the gcode (can read metadata, thumbnails, etc) ‒
  encryption key is only available through the key blocks, encrypted by RSA
  keys.
* One printer can't forge data for another printer ‒ doesn't know the another
  printer's verify key.
* Blocks can't be repeated, omitted, reordered ‒ position is part of the
  signature. Note: printer must bail out in case there's eg. an unencrypted
  (possibly "effectively empty" gcode block) or an unknown block ‒ so attacker
  can't remove a block by replacing it with a thud.

## Can be generated

1. Generate plain-text metadata, get a hash; write out.
2. Generate the keys & key blocks blocks, get a hash (and keep in RAM for a
   while).
3. Generate the identity block (we need the hash from the key blocks for that),
   write it out.
4. Write out the key blocks.
5. Generate stream of gcode blocks, encrypt each and write out; mark the last
   one with the last flag.

## Can be verified & printed

### Before print

1. Find the identity section and verify.
   - This is the place we check if the identity is trusted or if we are allowed
     to print untrusted gcode.
   - If there's no identity block, we consider the gcode not encrypted or signed
     and treat it as untrusted for the purpose of allowing / disallowing the
     print.
2. Hash the plain-text metadata and verify.
3. Hash the key blocks and verify.
4. Check there are no "gaps" between metadata & identity, identity & keys and
   keys and the first gcode block.
5. Find the relevant key block, decrypt, extract keys.

### During print

1. Seek to the first gcode block.
2. Verify its HMAC tag (the one corresponding to us) (needs to read it once).
   - Bail out if it doesn't match, report error.
3. Decrypt it as it is being printed (read second time).
4. Go to the next block and see that there's no "gap" in between and that the
   next block is also encrypted.
   - During streaming, we need to wait for it to arrive.
5. Repeat until the end of the file.
   - If the file terminates early, report an error.
   - If there's a `last`-marked block and some other block / data follows,
     report an error.

### On-demand scan

We could just omit the step 3. above (decryption and printing) to perform a
verification.

Maybe we could also run such verification scan in the background during a
preview and ask if the user wants to start printing right away or wait for it to
finish first if they click „Print“ before we are done.

## Implementation tricks

The identity block and key blocks are after all the plain-text metadata. That is
because the format doesn't allow ignoring unknown types of blocks (it needs to
know the type to figure out its length) and existing software is unable to read
"past" the newly added blocks. This way, existing software (eg. Connect) still
can decode the metadata and thumbnails (and it won't be able to read the
encrypted data anyway).

For this reason, it is not necessary to increment the whole-file format version
(which would also break compatibility for existing software).
