#!/usr/bin/env python
import sys
import os
import io
from hashlib import sha256
from typing import Optional
from binascii import crc32
from enum import IntEnum
from pathlib import Path
import struct
import hmac
import argparse

from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import serialization as crypto_serialization
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.asymmetric.types import PublicKeyTypes
from cryptography.hazmat.primitives.asymmetric.types import PrivateKeyTypes

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding as aes_padding

SIGN_SIZE = 256
HASH_SIZE = 32
AES_KEY_SIZE = 16
HMAC_SIZE = 32
KEY_BLOCK_SIZE = 512
CRC32_SIZE = 4


def aes_cbc_encrypt(message, key, iv):
    # Initialize the AES cipher in CBC mode with the provided key and IV
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    encryptor = cipher.encryptor()

    # Pad the message to be a multiple of the block size (16 bytes)
    padder = aes_padding.PKCS7(algorithms.AES.block_size).padder()
    padded_message = padder.update(message) + padder.finalize()

    # Encrypt the padded message
    encrypted_message = encryptor.update(padded_message) + encryptor.finalize()

    return encrypted_message


def aes_cbc_decrypt(encrypted_message, key, iv):
    # Initialize the AES cipher in CBC mode with the provided key and IV
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    decryptor = cipher.decryptor()

    # Decrypt the message
    padded_message = decryptor.update(encrypted_message) + decryptor.finalize()

    # Unpad the message
    unpadder = aes_padding.PKCS7(algorithms.AES.block_size).unpadder()
    message = unpadder.update(padded_message) + unpadder.finalize()

    return message


class ChecksumType(IntEnum):
    NONE = 0
    CRC32 = 1


class BlockType(IntEnum):
    FileMetadata = 0
    Gcode = 1
    SlicerMetadata = 2
    PrinterMetadata = 3
    PrintMetadata = 4
    Thumbnail = 5
    IdentityBlock = 6
    KeyBlock = 7
    EncryptedBlock = 8


class Compression(IntEnum):
    No = 0
    Deflate = 1
    HeatShrink_11_4 = 2
    HeatShrink_12_4 = 3


class IdentityBlockSign(IntEnum):
    RSA = 0


class IdentityFlags(IntEnum):
    ONE_TIME_IDENTITY = 0b1


class KeyBlockEncryption(IntEnum):
    No = 0
    RSA_ENC_SHA256_SIGN = 1


class EncryptedBlockEncryption(IntEnum):
    AES128_CBC_SHA256_HMAC = 0


class FileHeader:

    def __init__(self, file) -> None:
        self.magic, self.version, self.checksumType = struct.unpack(
            "<4sIh", file.read(10))

    def bytes(self) -> bytes:
        return struct.pack("<4sIh", self.magic, self.version,
                           self.checksumType)

    def check(self):
        assert (self.magic == b"GCDE")
        assert (self.version == 1)
        assert (self.checksumType in [0, 1])


def paramsSize(block_type: BlockType) -> int:
    if block_type == BlockType.Thumbnail:
        return 6
    elif block_type == BlockType.IdentityBlock:
        return 3
    elif block_type == BlockType.EncryptedBlock:
        return 3
    else:
        return 2


class BlockHeader:

    def __init__(self, header_type, compression, uncompressed_size,
                 compressed_size) -> None:
        self.type = header_type
        self.compression = compression
        self.uncompressed_size = uncompressed_size
        self.compressed_size = compressed_size

    def bytes(self) -> bytes:
        data = struct.pack("<hhI", self.type, self.compression,
                           self.uncompressed_size)
        if self.compression != 0:
            data += struct.pack("<I", self.compressed_size)
        return data

    def size(self) -> int:
        return 8 if self.compression == 0 else 12

    def payloadSizeWithParams(self):
        return paramsSize(self.type) + self.compressed_size


def readBlockHeader(file) -> BlockHeader:
    header_type, compression, uncompressed_size = struct.unpack(
        "<hhI", file.read(8))
    if compression == 0:
        compressed_size = uncompressed_size
    else:
        compressed_size = struct.unpack("<I", file.read(4))[0]
    return BlockHeader(header_type, compression, uncompressed_size,
                       compressed_size)


class IdentityBlock:

    def __init__(self) -> None:
        self.slicer_public_key_bytes = bytes()
        self.identity_name = bytes()
        self.intro_hash = bytes()
        self.key_bloks_hash = bytes()
        self.header = BlockHeader(BlockType.IdentityBlock, Compression.No,
                                  self.dataSize(), self.dataSize())
        self.enc_algo = IdentityBlockSign.RSA
        self.identity_flags = 0
        self.sign = bytes()

    def generate(self, slicer_pub_key, name, intro_hash, key_block_hash,
                 enc_algo, identity_flags):
        self.enc_algo = enc_algo
        self.identity_flags = identity_flags
        self.slicer_public_key_bytes = slicer_pub_key
        self.slicer_pub_key = crypto_serialization.load_der_public_key(
            self.slicer_public_key_bytes)
        self.identity_name = name
        self.intro_hash = intro_hash
        self.key_bloks_hash = key_block_hash
        self.header = BlockHeader(BlockType.IdentityBlock, Compression.No,
                                  self.dataSize(), self.dataSize())

    def read(self, block: bytes):
        stream = io.BytesIO(block)
        enc_algo = struct.unpack("<h", stream.read(2))[0]
        identity_flags = struct.unpack("B", stream.read(1))[0]
        slicer_pub_key_len = struct.unpack("<h", stream.read(2))[0]
        slicer_pub_key = stream.read(slicer_pub_key_len)
        identity_name_len = struct.unpack("<B", stream.read(1))[0]
        identity_name = stream.read(identity_name_len)
        intro_hash = stream.read(32)
        key_blocks_hash = stream.read(32)
        self.sign = stream.read(SIGN_SIZE)
        self.generate(slicer_pub_key, identity_name, intro_hash,
                      key_blocks_hash, enc_algo, identity_flags)

    def bytesToSign(self) -> bytes:
        buffer = bytearray()
        buffer.extend(self.header.bytes())
        buffer.extend(self.enc_algo.to_bytes(2, "little"))
        buffer.extend(self.identity_flags.to_bytes(1, "little"))
        buffer.extend(struct.pack("<h", len(self.slicer_public_key_bytes)))
        buffer.extend(self.slicer_public_key_bytes)
        buffer.extend(struct.pack("<B", len(self.identity_name)))
        buffer.extend(self.identity_name)
        buffer.extend(self.intro_hash)
        buffer.extend(self.key_bloks_hash)
        return buffer

    def signature(self, slicer_private_key: PrivateKeyTypes) -> bytes:
        return sign_message(slicer_private_key, self.bytesToSign())

    def verify(self) -> None:
        assert (len(self.sign) == SIGN_SIZE)
        if self.enc_algo != IdentityBlockSign.RSA:
            sys.exit("Unsupported identity block sign algorithm")
        verify(self.slicer_pub_key, self.bytesToSign(), self.sign)

    def bytes(self, checksumType: ChecksumType,
              slicer_private_key: PrivateKeyTypes) -> bytes:
        buffer = bytearray()
        buffer.extend(self.bytesToSign())
        buffer.extend(self.signature(slicer_private_key))
        if checksumType == ChecksumType.CRC32:
            buffer.extend(crc32(buffer).to_bytes(4, 'little'))
        return buffer

    def dataSize(self) -> int:
        size = (2 + len(self.slicer_public_key_bytes) + 1 +
                len(self.identity_name) + len(self.intro_hash) +
                len(self.key_bloks_hash)) + SIGN_SIZE
        return size


class GcodeBlock:

    def __init__(self, block_header: BlockHeader, data: bytes) -> None:
        self.header = block_header
        self.params = data[:2]
        self.data = data[2:]


def rsaEncrypt(key, message):
    return key.encrypt(
        message,
        padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA256()),
                     algorithm=hashes.SHA256(),
                     label=None))


def rasDecrypt(key, message):
    return key.decrypt(
        message,
        padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA256()),
                     algorithm=hashes.SHA256(),
                     label=None))


def sign_message(key, message):
    return key.sign(
        message,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.DIGEST_LENGTH), hashes.SHA256())


def verify(key, message, signature):
    key.verify(
        signature, message,
        padding.PSS(mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.DIGEST_LENGTH), hashes.SHA256())


def rsa_sha256_sign_block_encrypt(slicer_private_key: PrivateKeyTypes,
                                  printer_pub_key: PublicKeyTypes, key_block):
    slicer_pub_key_hash = sha256(slicer_private_key.public_key().public_bytes(
        crypto_serialization.Encoding.DER,
        crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    printer_pub_key_hash = sha256(
        printer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    inner = slicer_pub_key_hash.digest() + printer_pub_key_hash.digest(
    ) + key_block
    outer = rsaEncrypt(printer_pub_key, inner)
    signature = sign_message(slicer_private_key, outer)

    return outer + signature


def rsa_sha256_sign_block_decrypt(slicer_pub_key, printer_pub_key,
                                  printer_private_key,
                                  enc_key_block) -> Optional[bytes]:
    sig = enc_key_block[len(enc_key_block) - SIGN_SIZE:]
    outer_msg = enc_key_block[:len(enc_key_block) - SIGN_SIZE]
    verify(slicer_pub_key, outer_msg, sig)

    try:
        inner_msg = rasDecrypt(printer_private_key, outer_msg)
    except:
        return None

    slicer_pub_key_hash_dec = inner_msg[:HASH_SIZE]
    printer_pub_key_hash_dec = inner_msg[HASH_SIZE:2 * HASH_SIZE]
    slicer_pub_key_hash = sha256(
        slicer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    printer_pub_key_hash = sha256(
        printer_pub_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))
    key_block = inner_msg[2 * HASH_SIZE:]
    if slicer_pub_key_hash.digest(
    ) != slicer_pub_key_hash_dec or printer_pub_key_hash.digest(
    ) != printer_pub_key_hash_dec:
        return None
    return key_block


def readHashWriteMetadata(file_header: FileHeader, in_file: io.BufferedReader,
                          out_file: io.BufferedWriter,
                          encrypt_all: bool) -> bytes:
    metadata_hash = sha256(file_header.bytes())
    out_file.write(file_header.bytes())

    def stop(block_type):
        if encrypt_all:
            return True
        else:
            return block_header.type == 1

    while True:
        block_header = readBlockHeader(in_file)
        if stop(block_header.type):
            in_file.seek(-block_header.size(), 1)
            break

        size = block_header.payloadSizeWithParams()
        block = in_file.read(size)
        metadata_hash.update(block_header.bytes())
        metadata_hash.update(block)
        out_file.write(block_header.bytes())
        out_file.write(block)
        if file_header.checksumType == ChecksumType.CRC32:
            crc = in_file.read(4)
            metadata_hash.update(crc)
            out_file.write(crc)

    return metadata_hash.digest()


def createEncryptedBlockAES(plain_data: bytes, iv: bytes, enc_aes_key: bytes,
                            key_blocks: list, checksumType: ChecksumType,
                            is_last: bool) -> bytes:
    result = bytearray()
    enc_data = aes_cbc_encrypt(plain_data, enc_aes_key, iv)
    size = len(enc_data) + len(key_blocks) * HMAC_SIZE
    block_header = BlockHeader(BlockType.EncryptedBlock, Compression.No, size,
                               size)
    last_blog_flag = (int(is_last)).to_bytes(1, 'little')
    params = EncryptedBlockEncryption.AES128_CBC_SHA256_HMAC.to_bytes(
        2, 'little') + last_blog_flag
    hmac_data = block_header.bytes() + iv + params + enc_data
    result.extend(block_header.bytes())
    result.extend(params)
    result.extend(enc_data)
    crc = crc32(block_header.bytes() + params + enc_data)
    for key_block in key_blocks:
        hmac_sig = hmac.new(key_block.signKey(), hmac_data, sha256).digest()
        result.extend(hmac_sig)
        crc = crc32(hmac_sig, crc)
    if checksumType == ChecksumType.CRC32:
        result.extend(crc.to_bytes(4, 'little'))
    return result


def decryptEncryptedBlockAES(enc_aes_key: bytes, sign_key: bytes,
                             block_header: bytes, iv: bytes, enc_data: bytes,
                             num_of_hmacs: int, hmac_index: int,
                             params: bytes):
    plain_block = decrypt_aes_block(enc_aes_key, sign_key, block_header, iv,
                                    enc_data, num_of_hmacs, hmac_index, params)
    decrypted_header = readBlockHeader(io.BytesIO(plain_block))
    block_data = plain_block[decrypted_header.size():]
    return decrypted_header, block_data


class KeyBlock:

    def __init__(self, enc_aes_key: bytes, printer_pub_key: PublicKeyTypes,
                 slicer_private_key: PrivateKeyTypes) -> None:
        sign_key = os.urandom(16)
        self.plain_key_block = enc_aes_key + sign_key
        if printer_pub_key:
            encryption = KeyBlockEncryption.RSA_ENC_SHA256_SIGN
            self.enc_key_block = rsa_sha256_sign_block_encrypt(
                slicer_private_key, printer_pub_key, self.plain_key_block)
        else:
            encryption = KeyBlockEncryption.No
            self.enc_key_block = self.plain_key_block
        self.params = encryption.to_bytes(2, 'little')
        self.header = BlockHeader(BlockType.KeyBlock, Compression.No,
                                  len(self.enc_key_block),
                                  len(self.enc_key_block))

    def encAesKey(self) -> bytes:
        return self.plain_key_block[:AES_KEY_SIZE]

    def signKey(self) -> bytes:
        return self.plain_key_block[AES_KEY_SIZE:]

    def bytes(self, checksum_type: ChecksumType) -> bytes:
        buffer = bytearray()
        buffer.extend(self.header.bytes())
        buffer.extend(self.params)
        buffer.extend(self.enc_key_block)
        if checksum_type == ChecksumType.CRC32:
            buffer.extend(crc32(buffer).to_bytes(4, 'little'))
        return buffer


def generateIdentityBlock(key_blocks_hash: bytes,
                          slicer_private_key: PrivateKeyTypes,
                          metadata_hash: bytes, identity_name: str,
                          one_time_identity: bool) -> IdentityBlock:
    slicer_public_key_bytes = slicer_private_key.public_key().public_bytes(
        crypto_serialization.Encoding.DER,
        crypto_serialization.PublicFormat.SubjectPublicKeyInfo)
    identity_block = IdentityBlock()
    identity_flags = 0
    if one_time_identity:
        identity_flags |= IdentityFlags.ONE_TIME_IDENTITY
    identity_block.generate(slicer_public_key_bytes, identity_name.encode(),
                            metadata_hash, key_blocks_hash,
                            IdentityBlockSign.RSA, identity_flags)

    return identity_block


def readEncryptAndWriteGcodeBlocks(out_file: io.BufferedWriter,
                                   in_file: io.BufferedReader,
                                   key_blocks: list, enc_aes_key: bytes,
                                   checksumType: ChecksumType) -> None:
    while True:
        block_header = readBlockHeader(in_file)
        size = block_header.payloadSizeWithParams()

        gcode_block = GcodeBlock(block_header, in_file.read(size))
        if checksumType == ChecksumType.CRC32:
            #Read and ignore the CRC, we will make a new one
            #TODO maybe check it first and report error if it does not match
            _ = in_file.read(CRC32_SIZE)

        iv = out_file.tell().to_bytes(16, 'little')
        plain_data = gcode_block.header.bytes(
        ) + gcode_block.params + gcode_block.data
        if in_file.tell() == os.fstat(in_file.fileno()).st_size:
            # last block, set the last flag
            out_file.write(
                createEncryptedBlockAES(plain_data, iv, enc_aes_key,
                                        key_blocks, checksumType, True))
            break
        else:
            out_file.write(
                createEncryptedBlockAES(plain_data, iv, enc_aes_key,
                                        key_blocks, checksumType, False))


def encrypt_bgcode(in_filename: str, out_filename: str, printer_pub_keys: list,
                   slicer_private_key: PrivateKeyTypes, identity_name: str,
                   one_time_identity: bool, encrypt_all: bool):
    print("Reading bgcode from:", in_filename)
    in_file = open(in_filename, mode='rb')
    print("Writing encrypted bgcode to:", out_filename)
    out_file = open(out_filename, 'wb')

    file_header = FileHeader(in_file)
    file_header.check()

    metadata_hash = readHashWriteMetadata(file_header, in_file, out_file,
                                          encrypt_all)

    enc_aes_key = os.urandom(16)
    key_blocks = []
    key_blocks_hash = sha256()

    def do_key(key):
        key_block = KeyBlock(enc_aes_key, key, slicer_private_key)
        key_blocks.append(key_block)
        key_blocks_hash.update(key_block.bytes(file_header.checksumType))

    if printer_pub_keys:
        for key in printer_pub_keys:
            do_key(key)
    else:
        do_key(None)

    identity_block = generateIdentityBlock(key_blocks_hash.digest(),
                                           slicer_private_key, metadata_hash,
                                           identity_name, one_time_identity)

    out_file.write(
        identity_block.bytes(file_header.checksumType, slicer_private_key))
    for key_block in key_blocks:
        out_file.write(key_block.bytes(file_header.checksumType))

    readEncryptAndWriteGcodeBlocks(out_file, in_file, key_blocks, enc_aes_key,
                                   file_header.checksumType)


def is_metadata_block(type) -> bool:
    match type:
        case BlockType.FileMetadata:
            return True
        case BlockType.PrinterMetadata:
            return True
        case BlockType.Thumbnail:
            return True
        case BlockType.PrintMetadata:
            return True
        case BlockType.SlicerMetadata:
            return True
        case _:
            return False


def decrypt_aes_block(aes_enc_key,
                      sign_key,
                      block_header_bytes,
                      iv,
                      block,
                      num_of_hmacs,
                      hmac_index,
                      params: bytes = bytes()) -> bytes:
    hmacs_begin = len(block) - num_of_hmacs * HMAC_SIZE
    enc_data = block[:hmacs_begin]
    our_hmac_begin = hmacs_begin + hmac_index * HMAC_SIZE
    our_hmac = block[our_hmac_begin:our_hmac_begin + HMAC_SIZE]
    hmac_data = block_header_bytes + iv + params + enc_data
    computed_hmac = hmac.new(sign_key, hmac_data, sha256)
    if not hmac.compare_digest(our_hmac, computed_hmac.digest()):
        sys.exit("HMAC mismatch")

    return aes_cbc_decrypt(enc_data, aes_enc_key, iv)


def decrypt_bgcode(in_filename, out_filename,
                   printer_private_key: PrivateKeyTypes):
    enc_file = open(in_filename, 'rb')
    file_header = FileHeader(enc_file)
    file_header.check()
    out_bytes = bytearray()
    out_bytes.extend(file_header.bytes())
    gcode_enc_key = bytes()
    gcode_sign_key = bytes()
    crc = bytes()
    identity_block = IdentityBlock()
    key_block_index = 0
    num_of_key_blocks = 0
    key_blocks_hash = sha256()
    #enc_key_blocks_data = []
    first_gcode_block = True
    is_last_block = False

    while True:
        iv = enc_file.tell().to_bytes(16, 'little')
        block_header = readBlockHeader(enc_file)
        size = block_header.payloadSizeWithParams()
        block = enc_file.read(size)
        if file_header.checksumType == ChecksumType.CRC32:
            crc = enc_file.read(4)
            gen_crc = crc32(block_header.bytes() + block)
            if int.from_bytes(crc, 'little') != gen_crc:
                sys.exit("Block CRC mismatch!!")

        if block_header.type == BlockType.EncryptedBlock:
            params = block[:paramsSize(BlockType.EncryptedBlock)]
            data = block[paramsSize(BlockType.EncryptedBlock):]
            algo = int.from_bytes(params[:2], 'little')
            is_last_block = int.from_bytes(params[2:], 'little')
            if algo == EncryptedBlockEncryption.AES128_CBC_SHA256_HMAC:
                block_header, block = decryptEncryptedBlockAES(
                    gcode_enc_key, gcode_sign_key, block_header.bytes(), iv,
                    data, num_of_key_blocks, key_block_index, params)
            else:
                sys.exit("Unsupported encryption algorithm")

        if is_metadata_block(block_header.type):
            out_bytes.extend(block_header.bytes())
            out_bytes.extend(block)
            if file_header.checksumType == ChecksumType.CRC32:
                out_bytes.extend(crc)
        match block_header.type:
            case BlockType.IdentityBlock:
                identity_block.read(block)
                metadata_hash = sha256(out_bytes).digest()
                if metadata_hash != identity_block.intro_hash:
                    sys.exit("Metadata hash mismatch!")
                print("Identity name: ", identity_block.identity_name)
                identity_block.verify()

            case BlockType.KeyBlock:
                num_of_key_blocks += 1
                key_blocks_hash.update(block_header.bytes() + block)
                if file_header.checksumType == ChecksumType.CRC32:
                    key_blocks_hash.update(crc)
                algo = int.from_bytes(block[:paramsSize(BlockType.KeyBlock)],
                                      'little')
                if algo == KeyBlockEncryption.RSA_ENC_SHA256_SIGN:
                    key_block = rsa_sha256_sign_block_decrypt(
                        identity_block.slicer_pub_key,
                        printer_private_key.public_key(), printer_private_key,
                        block[paramsSize(BlockType.KeyBlock):])
                    if key_block:
                        #to know which hmac to check
                        key_block_index = num_of_key_blocks - 1
                        gcode_enc_key = key_block[:AES_KEY_SIZE]
                        gcode_sign_key = key_block[AES_KEY_SIZE:]
                    else:
                        continue
                elif algo == KeyBlockEncryption.No:
                    key_block_index = num_of_key_blocks - 1
                    gcode_enc_key = block[:AES_KEY_SIZE]
                    gcode_sign_key = block[AES_KEY_SIZE:]
                else:
                    sys.exit("Unsupported asymetric crypto algorithm")

            case BlockType.Gcode:
                if first_gcode_block:
                    if key_blocks_hash.digest(
                    ) != identity_block.key_bloks_hash:
                        sys.exit("Key blocks hash mismatch!!")
                    first_gcode_block = False

                gcode_block = GcodeBlock(block_header, block)
                #handle empty gcode block
                if len(gcode_block.data) > 0:
                    out_bytes.extend(block_header.bytes())
                    out_bytes.extend(gcode_block.params)
                    out_bytes.extend(gcode_block.data)
                    if file_header.checksumType == ChecksumType.CRC32:
                        out_bytes.extend(
                            crc32(block_header.bytes() + gcode_block.params +
                                  gcode_block.data).to_bytes(4, 'little'))
                if is_last_block:
                    if enc_file.tell() == os.fstat(enc_file.fileno()).st_size:
                        out_file = open(out_filename, 'wb')
                        out_file.write(out_bytes)
                        print("Done succesfully")
                        sys.exit(0)
                    else:
                        sys.exit(
                            "Last gcode block is not at the end, appended data found!!"
                        )

        if enc_file.tell() == os.fstat(enc_file.fileno()).st_size:
            sys.exit("Truncated file!!")


def savePrivateKey(private_key, filename) -> None:
    private_key_file = open(filename, 'wb')
    private_key_file.write(
        private_key.private_bytes(
            encoding=crypto_serialization.Encoding.DER,
            format=crypto_serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=crypto_serialization.NoEncryption()))


def savePublicKey(public_key, filename) -> None:
    public_key_file = open(filename, 'wb')
    public_key_file.write(
        public_key.public_bytes(
            crypto_serialization.Encoding.DER,
            crypto_serialization.PublicFormat.SubjectPublicKeyInfo))


def generateKeys():
    printer_private_key = rsa.generate_private_key(public_exponent=65537,
                                                   key_size=2048)
    savePrivateKey(printer_private_key, "printer_private_key.der")

    printer_public_key = printer_private_key.public_key()
    savePublicKey(printer_public_key, "printer_public_key.der")

    slicer_private_key = rsa.generate_private_key(public_exponent=65537,
                                                  key_size=2048)
    savePrivateKey(slicer_private_key, "slicer_private_key.der")


def main():
    parser = argparse.ArgumentParser(
        prog="Bgcode encrypt/decrypt",
        description="Encrypts/decrypts bgcodes for e2ee",
        epilog="TADA")

    parser.add_argument("-e",
                        "--encrypt",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-d",
                        "--decrypt",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-g",
                        "--generate-keys",
                        action=argparse.BooleanOptionalAction)
    parser.add_argument("-oti",
                        "--one-time-identity",
                        action=argparse.BooleanOptionalAction,
                        default=False)
    parser.add_argument("-in", "--input-file", type=Path)
    parser.add_argument("-out", "--output-file", type=Path)
    parser.add_argument("-spk", "--slicer-private-key", type=Path)
    parser.add_argument("-ppk", "--printer-private-key", type=Path)
    parser.add_argument("-ppubk", "--printer-public-key", type=Path)
    parser.add_argument("-id", "--identity-name", type=str)
    parser.add_argument("-all",
                        "--encrypt-all",
                        action=argparse.BooleanOptionalAction,
                        default=False)
    parser.add_argument("-so",
                        "--sign-only",
                        action=argparse.BooleanOptionalAction,
                        default=False)

    args = parser.parse_args()
    if args.encrypt and args.decrypt:
        parser.error("Choose only one of --encrypt(-e), --decypt(-d)")

    if args.generate_keys:
        generateKeys()

    if args.encrypt and (not args.slicer_private_key
                         or not (args.printer_public_key or args.sign_only)
                         or not args.input_file or not args.output_file
                         or not args.identity_name):
        parser.error("Invalid arguments")
    if args.decrypt and (not args.printer_private_key or not args.input_file
                         or not args.output_file):
        parser.error("Invalid arguments")

    if args.encrypt:
        with open(args.slicer_private_key, "br") as key_file:
            slicer_private_key = crypto_serialization.load_der_private_key(
                key_file.read(), None)
        printer_keys = []
        if not args.sign_only:
            with open(args.printer_public_key, "br") as key_file:
                printer_public_key = crypto_serialization.load_der_public_key(
                    key_file.read())

            printer_keys.append(printer_public_key)

        encrypt_bgcode(args.input_file, args.output_file, printer_keys,
                       slicer_private_key, args.identity_name,
                       args.one_time_identity, args.encrypt_all)

    elif args.decrypt:
        with open(args.printer_private_key, "br") as key_file:
            printer_private_key = crypto_serialization.load_der_private_key(
                key_file.read(), None)
        with open(args.printer_public_key, "br") as key_file:
            printer_public_key = crypto_serialization.load_der_public_key(
                key_file.read())

        decrypt_bgcode(args.input_file, args.output_file, printer_private_key)

    if not args.encrypt and not args.decrypt and not args.generate_keys:
        parser.error("Nothing to do")


main()
