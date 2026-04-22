# Gcode encryption/decryption tool for testing purposes
This script can encrypt bgcodes for end to end encryption feature and decrypt them back. It can also generate the key simulating slicer and printer RSA private/public keys in DER format.

## Example usage

### Generating keys
python encrypt_bgcode.py -g

### Encryption
python  encrypt_bgcode.py --encrypt -in in.bgcode -out out.bgcode -spk slicer_private_key.der -ppubk printer_public_key.der

### Decryption
python  encrypt_bgcode.py --decrypt -in out.bgcode -out decrypted.bgcode  -ppk printer_private_key.der -ppubk printer_public_key.der
