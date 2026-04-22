target_sources(
  firmware
  PRIVATE decryptor.cpp
          e2ee.cpp
          hmac.cpp
          key.cpp
          movable_aes_context.cpp
          sha256_multiuse.cpp
          utils.cpp
  )
