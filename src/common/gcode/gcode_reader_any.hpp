#pragma once

#include "gcode_reader_binary.hpp"
#include "gcode_reader_closed.hpp"
#include "gcode_reader_plaintext.hpp"
#include <variant>

#include <config_store/store_instance.hpp>
#include <option/has_e2ee_support.h>
#if HAS_E2EE_SUPPORT()
    #include <e2ee/identity_check_levels.hpp>
#endif

/**
 * @brief Container that can open and read any gcode regardless of what type it is.
 *        Stores Plain/PrusaPack reader inside, so dynamic alocation is not needed.
 *        Also handles destruction & closing of files.
 */
class AnyGcodeFormatReader {
public:
    AnyGcodeFormatReader();
    AnyGcodeFormatReader(const char *filename, bool allow_decryption = false
#if HAS_E2EE_SUPPORT()
        ,
        e2ee::IdentityCheckLevel identity_check_lvl = config_store().identity_check.get()
#endif
    );
    AnyGcodeFormatReader(const AnyGcodeFormatReader &) = delete;
    AnyGcodeFormatReader &operator=(const AnyGcodeFormatReader &) = delete;
    AnyGcodeFormatReader(AnyGcodeFormatReader &&);
    AnyGcodeFormatReader &operator=(AnyGcodeFormatReader &&);
    ~AnyGcodeFormatReader();

    bool open(const char *filename, bool allow_decryption = false
#if HAS_E2EE_SUPPORT()
        ,
        e2ee::IdentityCheckLevel identity_check_lvl = config_store().identity_check.get()
#endif
    );

    void close();

    IGcodeReader *get(); // never returns nullptr
    IGcodeReader &operator*() { return *get(); }
    IGcodeReader *operator->() { return get(); }

    /**
     * @brief Return true if openning was successfull
     *
     * @return true
     * @return false
     */
    bool is_open() const;

private:
    typedef std::variant<ClosedReader, PlainGcodeReader, PrusaPackGcodeReader> storage_type_t;
    storage_type_t storage;
};
