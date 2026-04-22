#include "test_files.hpp"

#include <gcode_reader_any.hpp>
#include <common/thumbnail_sizes.hpp>
#include <catch2/catch.hpp>

#include <deque>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

namespace {

// These are made from the test_binary_no_compression.bgcode by mangling a specific CRC.
// See the utils/crckill.

// A thumbnail with bad CRC
constexpr static const char *BINARY_BAD_CRC_INTRO = "test_bad_crc_intro.bgcode";
// The CRC on the first gcode block
constexpr static const char *BINARY_BAD_CRC_FIRST_GCODE = "test_bad_crc_first_gcode.bgcode";
// Some later gcode block
constexpr static const char *BINARY_BAD_CRC_OTHER_GCODE = "test_bad_crc_gcode.bgcode";

constexpr static const char *BINARY_ENCRYPTED_CORRECT_GCODE = "test_encrypted_gcode_correct.bgcode";

constexpr static const std::string_view DUMMY_DATA_LONG = "; Short line\n"
                                                          ";Long line012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789\n"
                                                          ";Another short line";
constexpr static const std::string_view DUMMY_DATA_EXACT = ";01234567890123456789012345678901234567890123456789012345678901234567890123456789\n"
                                                           ";Another line";
constexpr static const std::string_view DUMMY_DATA_EXACT_EOF = ";01234567890123456789012345678901234567890123456789012345678901234567890123456789";
constexpr static const std::string_view DUMMY_DATA_ERR = ";01234567890123456789012345678901234567890123456789012345678901234567890123456789012345";

struct TestFile {
    const char *filename;
    // Can the file be indexed?
    bool indexed;
    // Are metadata & comments part of "general" gcode?
    bool unified;
    bool has_qoi_thumbnails;
    // BUG: When restoring, we get different data. Skip them for now, as it's a different issue.
    bool skip_restore_end_test;
    // Fully encrypted gcodes have metadata, but we are not (yet) able to
    // decode and read them. So, for us, it looks like there's no metadata or
    // thumbnail section.
    bool has_plain_metadata;
};

const std::vector<TestFile> test_files = {
    { PLAIN_TEST_FILE, false, true, false, false, true },
    { BINARY_NO_COMPRESSION_FILE, true, false, false, false, true },
    { BINARY_MEATPACK_FILE, true, false, false, false, true },
    { BINARY_HEATSHRINK_FILE, true, false, false, false, true },
    { BINARY_HEATSHRINK_MEATPACK_FILE, true, false, false, false, true },
    { BINARY_ENCRYPTED_CORRECT_GCODE, true, false, false, false, true },
    { NEW_PLAIN, false, true, true, false, true },
    { NEW_BINARY, true, false, true, true, true },
    { NEW_BINARY_META_BEFORE, true, false, true, true, true },
    { NEW_BINARY_META_AFTER, true, false, true, true, true },
    { NEW_ENCRYPTED, true, false, true, true, true },
    { NEW_ENCRYPTED_MULTI, true, false, true, true, true },
    { NEW_ENCRYPTED_POLY, true, false, true, true, true },
    { NEW_ENCRYPTED_FULLY, true, false, true, true, false },
    { NEW_SIGNED, true, false, true, true, true },
};

using State = transfers::PartialFile::State;
using ValidPart = transfers::PartialFile::ValidPart;
using std::nullopt;
using std::string_view;

IGcodeReader::Result_t stream_get_block(IGcodeReader &reader, char *target, size_t &size) {
    auto end = target + size;
    while (target != end) {
        const auto res = reader.stream_getc(*(target++));
        if (res != IGcodeReader::Result_t::RESULT_OK) {
            size -= (end - target);
            return res;
        }
    }
    return IGcodeReader::Result_t::RESULT_OK;
};

struct DummyReader : public GcodeReaderCommon {
    std::deque<char> data;
    Result_t final_result;

    DummyReader(const std::string_view &input, Result_t final_result)
        : data(input.begin(), input.end())
        , final_result(final_result) {
        // Grouchy Smurf: I hate pointer-to-member-function casts
        ptr_stream_getc = static_cast<stream_getc_type>(&DummyReader::dummy_getc);
    }

    virtual bool stream_metadata_start(const Index *) override {
        return true;
    }

    virtual Result_t stream_gcode_start(uint32_t, bool, const Index *) override {
        return Result_t::RESULT_OK;
    }

    virtual AbstractByteReader *stream_thumbnail_start(uint16_t, uint16_t, ImgType, bool) override {
        return nullptr;
    }

    virtual uint32_t get_gcode_stream_size_estimate() override {
        return 0;
    }

    virtual uint32_t get_gcode_stream_size() override {
        return 0;
    }

    virtual bool valid_for_print([[maybe_unused]] bool full_check) override {
        return true;
    }

    virtual Result_t stream_get_line(GcodeBuffer &buffer, Continuations continuations) override {
        return stream_get_line_common(buffer, continuations);
    }

    Result_t dummy_getc(char &out) {
        if (data.empty()) {
            return final_result;
        } else {
            out = data.front();
            data.pop_front();
            return Result_t::RESULT_OK;
        }
    }

    virtual StreamRestoreInfo get_restore_info() override { return {}; }

    virtual void set_restore_info(const StreamRestoreInfo &) override {}
};

} // namespace

TEST_CASE("Extract data", "[GcodeReader]") {
    auto run_test = [](IGcodeReader *r, std::string base_name, bool image, bool has_metadata) {
        GcodeBuffer buffer;
        if (has_metadata) {
            REQUIRE(r->stream_metadata_start());
            std::ofstream fs(base_name + "-metadata.txt", std::ofstream::out);
            IGcodeReader::Result_t result;
            while ((result = r->stream_get_line(buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
                fs << buffer.line.begin << std::endl;
            }
            REQUIRE(result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        } else {
            REQUIRE_FALSE(r->stream_metadata_start());
        }

        {
            // Needed for the encrypted file, so that we do all the initial
            // asymmetric decryption stuff
            REQUIRE(r->valid_for_print(true));
            REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
            std::ofstream fs(base_name + "-gcode.gcode", std::ofstream::out);
            IGcodeReader::Result_t result;
            while ((result = r->stream_get_line(buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
                fs << buffer.line.begin << std::endl;
            }
            REQUIRE(result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        }
        if (image) {
            REQUIRE(r->stream_thumbnail_start(440, 240, IGcodeReader::ImgType::PNG, false));
            std::ofstream fs(base_name + "-thumb.png", std::ofstream::out);
            char c;
            IGcodeReader::Result_t result;
            while ((result = r->stream_getc(c)) == IGcodeReader::Result_t::RESULT_OK) {
                fs << c;
            }
            REQUIRE(result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        }
    };

    for (auto &test : test_files) {
        SECTION(std::string("Test-file: ") + test.filename) {
            AnyGcodeFormatReader reader(test.filename);
            REQUIRE(reader.is_open());
            run_test(reader.get(), test.filename, !test.has_qoi_thumbnails, test.has_plain_metadata);
        }
    }
}

TEST_CASE("Indexed readers", "[GcodeReader]") {
    for (auto &test : test_files) {
        SECTION(std::string("Test-file: ") + test.filename) {
            if (!test.has_plain_metadata) {
                // We skip the test with fully-encrypted bgcode, for several reasons:
                //
                // * The index is generated before we decrypt the content of
                //   the file. That means it can't reliably find the right place
                //   where the gcode actually starts. Therefore, the test fails.
                // * However, in reality, we use the index only in gcode_info
                //   and there we do _not_ decrypt at all. Therefore, the wrong
                //   place where to start decrypting is irrelevant for that use
                //   case.
                // * For real solution of the problem, we would have to allow
                //   the generate_index to look into the decrypted data. But that
                //   doesn't correspond to a real life situation.
                //
                // A real solution for this is needed as part of BFW-7432.
                // Until then, we just skip this test.
                continue;
            }
            AnyGcodeFormatReader reader(test.filename);
            REQUIRE(reader.is_open());
            IGcodeReader::Index index;
            REQUIRE(!index.indexed());
            // Note: These sizes are for mk4, tests are something like mini.
            index.thumbnails[0] = { 440, 240, IGcodeReader::ImgType::PNG };
            // The new files do have the mini sizes.
            index.thumbnails[1] = { thumbnail_sizes::preview_thumbnail_width, thumbnail_sizes::preview_thumbnail_height, IGcodeReader::ImgType::QOI };
            reader->generate_index(index);
            REQUIRE(index.indexed() == test.indexed);
            if (test.indexed) {
                CHECK(index.gcode != IGcodeReader::Index::not_indexed);
                CHECK(index.gcode != IGcodeReader::Index::not_present);
                if (test.has_plain_metadata) {
                    CHECK(index.metadata != IGcodeReader::Index::not_indexed);
                    CHECK(index.metadata != IGcodeReader::Index::not_present);
                } else {
                    CHECK(index.metadata == IGcodeReader::Index::not_present);
                }
                for (const auto &thumb : index.thumbnails) {
                    CHECK(thumb.position != IGcodeReader::Index::not_indexed);
                }
                if (test.has_plain_metadata) {
                    CHECK((index.thumbnails[0].position != IGcodeReader::Index::not_present) == !test.has_qoi_thumbnails);
                    CHECK((index.thumbnails[1].position != IGcodeReader::Index::not_present) == test.has_qoi_thumbnails);
                } else {
                    CHECK(index.thumbnails[0].position == IGcodeReader::Index::not_present);
                    CHECK(index.thumbnails[1].position == IGcodeReader::Index::not_present);
                }
                CHECK(index.thumbnails[2].position == IGcodeReader::Index::not_present);

                // Compare indexed vs non-indexed access.
                AnyGcodeFormatReader reader2(test.filename);
                // Needed for the encrypted file, so that we do all the initial
                // asymmetric decryption stuff
                REQUIRE(reader->valid_for_print(true));
                REQUIRE(reader2->valid_for_print(true));

                REQUIRE(reader->stream_gcode_start(0, false, &index) == IGcodeReader::Result_t::RESULT_OK);
                REQUIRE(reader2->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);

                GcodeBuffer buffer;
                GcodeBuffer buffer2;

                IGcodeReader::Result_t result;
                while ((result = reader->stream_get_line(buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
                    REQUIRE(reader2->stream_get_line(buffer2, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
                    REQUIRE(strcmp(buffer.line.c_str(), buffer2.line.c_str()) == 0);
                }

                REQUIRE(reader2->stream_get_line(buffer2, IGcodeReader::Continuations::Discard) == result);

                REQUIRE(reader->stream_metadata_start(&index));
                REQUIRE(reader2->stream_metadata_start());

                while ((result = reader->stream_get_line(buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
                    REQUIRE(reader2->stream_get_line(buffer2, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
                    REQUIRE(strcmp(buffer.line.c_str(), buffer2.line.c_str()) == 0);
                }

                REQUIRE(reader2->stream_get_line(buffer2, IGcodeReader::Continuations::Discard) == result);
            }
        }
    }
}

TEST_CASE("Mixed vs split readers", "[GcodeReader]") {
    for (auto &test : test_files) {
        SECTION(std::string("Test-file: ") + test.filename) {
            AnyGcodeFormatReader reader(test.filename);
            REQUIRE(reader.is_open());
            REQUIRE(reader->valid_for_print(true));

            bool seen_meta = false;
            bool seen_thumbnail = false;

            REQUIRE(reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
            GcodeBuffer buffer;

            while (reader->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK) {
                // Note: This parsing would be insufficient in case of dealing
                // with arbitrary gcode with some variability (eg. amount of
                // whitespace before or after the tokens), but good enough with
                // tests with known test data.
                constexpr const char *thumb = "; thumbnail begin";
                if (strncmp(buffer.line.c_str(), thumb, strlen(thumb)) == 0) {
                    seen_thumbnail = true;
                }
                bool is_comment = buffer.line.front() == ';';
                auto parsed = buffer.line.parse_metadata();
                if (!parsed.first.is_empty() && !parsed.second.is_empty() && is_comment) {
                    seen_meta = true;
                }
            }

            if (test.unified) {
                // Unfortunately, even the binary gcodes do contain comments in
                // the gcode section that look like metadata (how is it even
                // possible?!).
                CHECK(seen_meta);
            }
            CHECK(seen_thumbnail == test.unified);
        }
    }
}

TEST_CASE("stream restore at offset", "[GcodeReader]") {
    // tests reads reader1 continuously, and seeks in reader2 to same position
    //  as where it is in reader1 and compares if they give same results while seeking to middle of file

    auto run_test = [](IGcodeReader &reader1, const char *filename, bool skip_end_compare) {
        const size_t sizes[] = { 101, 103, 107, 109, 113, 3037, 3041, 3049, 3061, 3067 };
        std::unique_ptr<char[]> buffer1(new char[*std::max_element(sizes, sizes + std::size(sizes))]);
        std::unique_ptr<char[]> buffer2(new char[*std::max_element(sizes, sizes + std::size(sizes))]);
        // Needed for the encrypted file, so that we do all the initial
        // asymmetric decryption stuff
        REQUIRE(reader1.valid_for_print(true));
        long unsigned int offset = 0;
        REQUIRE(reader1.stream_gcode_start(0) == IGcodeReader::Result_t::RESULT_OK);
        size_t ctr = 0;

        GCodeReaderStreamRestoreInfo restore_info;
        bool has_restore_info = false;
        while (true) {
            auto size = sizes[ctr++ % std::size(sizes)]; // pick next size to read

            auto reader2_anyformat = AnyGcodeFormatReader(filename);
            auto reader2 = reader2_anyformat.get();
            if (has_restore_info) {
                reader2->set_restore_info(restore_info);
            }
            // Needed for the encrypted file, so that we do all the initial
            // asymmetric decryption stuff
            REQUIRE(reader2->valid_for_print(true));
            REQUIRE(reader2->stream_gcode_start(offset) == IGcodeReader::Result_t::RESULT_OK);

            auto size1 = size;
            auto res1 = stream_get_block(reader1, buffer1.get(), size1);
            auto size2 = size;
            auto res2 = stream_get_block(*reader2, buffer2.get(), size2);

            REQUIRE(res1 == res2);
            REQUIRE(((res1 == IGcodeReader::Result_t::RESULT_EOF) || (res1 == IGcodeReader::Result_t::RESULT_OK)));
            if (res1 == IGcodeReader::Result_t::RESULT_OK) {
                // if read went OK, requested number of bytes have to be returned, not less
                REQUIRE(size == size1);
            }
            REQUIRE(size1 == size2);

            INFO("offset " << offset << " size " << size << " res " << (int)res1 << " size out " << size1);
            if (!skip_end_compare) {
                REQUIRE(memcmp(buffer1.get(), buffer2.get(), size1) == 0);
            }

            if (res1 == IGcodeReader::Result_t::RESULT_EOF) {
                break;
            }
            REQUIRE(res1 == IGcodeReader::Result_t::RESULT_OK);

            offset += size;
            // read something from the buffer2, so that file position moves and we could see if stream_gcode_start doesn't return to correct position
            stream_get_block(*reader2, buffer2.get(), size);

            restore_info = reader2->get_restore_info();
            has_restore_info = true;
        }
    };

    for (auto &test : test_files) {
        SECTION(std::string("Test-file: ") + test.filename) {
            auto reader1 = AnyGcodeFormatReader(test.filename);
            REQUIRE(reader1.is_open());
            run_test(*reader1.get(), test.filename, test.skip_restore_end_test);
        }
    }
}

TEST_CASE("copy & move operators", "[GcodeReader]") {
    GcodeBuffer buffer;

    // open file
    auto reader = AnyGcodeFormatReader(PLAIN_TEST_FILE);
    REQUIRE(reader.is_open());
    REQUIRE(reader.get() != nullptr);
    REQUIRE(reader.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(reader.get()->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);

    // copy it elsewhere, and check that it can read file
    auto reader2 = std::move(reader); // move operator
    REQUIRE(reader2.is_open());
    REQUIRE(reader2.get() != nullptr);
    REQUIRE(reader2.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(reader2.get()->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);

    auto reader3(std::move(reader2)); // move constructor
    REQUIRE(reader3.is_open());
    REQUIRE(reader3.get() != nullptr);
    REQUIRE(reader3.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(reader3.get()->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);

    // but its not possible to read from original place
    REQUIRE(!reader.is_open());
    REQUIRE(!reader2.is_open());
}

TEST_CASE("validity-plain", "[GcodeReader]") {
    auto reader = AnyGcodeFormatReader(PLAIN_TEST_FILE);
    auto r = dynamic_cast<PlainGcodeReader *>(reader.get());
    REQUIRE(r != nullptr);

    struct stat st = {};
    REQUIRE(stat(PLAIN_TEST_FILE, &st) == 0);
    size_t size = st.st_size;
    r->set_validity(State { nullopt, nullopt, size });

    REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);

    GcodeBuffer buffer;
    // Not available yet
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
    r->set_validity(State { ValidPart(0, 0), nullopt, size });
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
    r->set_validity(State { ValidPart(0, 1024), nullopt, size });
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);

    size_t len = buffer.line.end - buffer.line.begin;
    auto f = unique_file_ptr(fopen(PLAIN_TEST_FILE, "r"));
    char buff_exp[len];
    REQUIRE(fread(buff_exp, len, 1, f.get()) == 1);

    // stream_get_line zero terminates, so we do the same so it compares OK
    buff_exp[len] = '\0';
    REQUIRE(string_view(buffer.line.begin, buffer.line.end) == string_view(buff_exp, buff_exp + len));
}

TEST_CASE("validity-bgcode", "[GcodeReader]") {
    auto reader = AnyGcodeFormatReader(BINARY_HEATSHRINK_MEATPACK_FILE);
    auto r = dynamic_cast<PrusaPackGcodeReader *>(reader.get());
    REQUIRE(r != nullptr);

    struct stat st = {};
    REQUIRE(stat(BINARY_HEATSHRINK_MEATPACK_FILE, &st) == 0);
    size_t size = st.st_size;

    GcodeBuffer buffer;
    // Not available yet
    r->set_validity(State { nullopt, nullopt, size });
    REQUIRE(r->stream_metadata_start() == false);
    REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
    r->set_validity(State { ValidPart(0, 0), nullopt, size });
    REQUIRE(r->stream_metadata_start() == false);
    REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);

    // just printer metadata is valid
    r->set_validity(State { ValidPart(0, 613), nullopt, size });
    REQUIRE(r->stream_metadata_start());
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);

    // all metadata & first gcode block is valid
    r->set_validity(State { ValidPart(0, 119731), nullopt, size });
    REQUIRE(r->stream_metadata_start());
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);

    // read entire first block,, that should go fine, then it shoudl return OUT_OF_RANGE on first character on next block
    size_t first_block_size = 59693;
    char c;
    while (first_block_size--) {
        REQUIRE(r->stream_getc(c) == IGcodeReader::Result_t::RESULT_OK);
    }
    REQUIRE(r->stream_getc(c) == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
}

/*
 * Make sure we are never willing to print past the already downloaded boundary.
 *
 * Try each prefix of the file and try to read from it as far as possible and
 * check we don't read the gargabe from the file or that we don't triger a CRC
 * error (which would both indicate we access some invalid data).
 *
 * To allow running it inside the CI, the test is limited to only some random
 * subset of the prefixes, it takes a long time otherwise. But it can easily be
 * enabled and left for few hours to actually run in full (and it was run that
 * way originally).
 */
TEST_CASE("validity-single-increments", "[GcodeReader]") {
    // Testing both textual and binary gcodes
    const char *inputs[] = { BINARY_HEATSHRINK_MEATPACK_FILE, PLAIN_TEST_FILE };
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        const char *input = inputs[i];
        INFO("Testing file " << input);
        struct stat st = {};
        REQUIRE(stat(input, &st) == 0);
        size_t size = st.st_size;
        // tmpnam is considered deprecated, but we don't know of a portable
        // replacement. We use the reentrant version (with passing a buffer)
        // and we are in tests only, so that's not a big deal.
        char tfile[L_tmpnam + 10];
        char *tmp_file_name = tmpnam(tfile);
        if (strcmp(input, PLAIN_TEST_FILE) == 0) {
            strcat(tmp_file_name, ".gcode");
        } else {
            strcat(tmp_file_name, ".bgcode");
        }
        INFO("TMP file " << tmp_file_name);
        struct Deleter {
            const char *path;
            ~Deleter() {
                unlink(path);
            }
        };
        Deleter deleter { tmp_file_name };

        for (size_t j = 0; j < size; j++) {
            // The test would be taking waaay too long in its full
            // configuration. Preserving it in somewhat more lightweight form
            // that skips most of the sizes.
            if (random() % 2000 != 0 || j == 0) {
                continue;
            }

            {
                unique_file_ptr fin(fopen(input, "rb"));
                REQUIRE(fin.get() != nullptr);

                unique_file_ptr ftmp(fopen(tmp_file_name, "wb"));
                REQUIRE(ftmp.get() != nullptr);

                // Put a prefix into the file
                for (size_t k = 0; k < j; k++) {
                    int c = fgetc(fin.get());
                    REQUIRE(c != EOF);
                    REQUIRE(fputc(c, ftmp.get()) != EOF);
                }

                // Fill the rest with binary garbage.
                for (size_t k = j; k < size; k++) {
                    REQUIRE(fputc(0xFF, ftmp.get()) != EOF);
                }
            }

            INFO("Size " << j);

            auto reader = AnyGcodeFormatReader(tmp_file_name);

            if (auto *r = dynamic_cast<PrusaPackGcodeReader *>(reader.get()); r != nullptr) {
                r->set_validity(State { ValidPart(0, j), nullopt, size });
            } else if (auto *r = dynamic_cast<PlainGcodeReader *>(reader.get()); r != nullptr) {
                r->set_validity(State { ValidPart(0, j), nullopt, size });
            } else {
                FAIL("Reader not open");
            }

            auto *r = reader.get();

            auto res = r->stream_gcode_start();
            INFO("Result " << (int)res);
            switch (res) {
            case IGcodeReader::Result_t::RESULT_OUT_OF_RANGE:
                // Not enough data to even start streaming gcode; that's fine.
                continue;
            case IGcodeReader::Result_t::RESULT_OK: {
                res = IGcodeReader::Result_t::RESULT_OK;
                while (res == IGcodeReader::Result_t::RESULT_OK) {
                    char c;
                    res = r->stream_getc(c);
                    // Check we are not reading plaintext garbage
                    // (the bgcode already checks CRCs)
                    REQUIRE(static_cast<uint8_t>(c) != 0xFF);
                };
                bool ok = (res == IGcodeReader::Result_t::RESULT_EOF) || (res == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
                REQUIRE(ok);

                REQUIRE(r->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);

                res = IGcodeReader::Result_t::RESULT_OK;
                while (res == IGcodeReader::Result_t::RESULT_OK) {
                    GcodeBuffer buffer;
                    res = r->stream_get_line(buffer, IGcodeReader::Continuations::Discard);
                    if (res == IGcodeReader::Result_t::RESULT_OK) {
                        // Check we are not reading plaintext garbage
                        // (the bgcode already checks CRCs)
                        REQUIRE(strchr(buffer.line.c_str(), 0xFF) == nullptr);
                    }
                };
                ok = (res == IGcodeReader::Result_t::RESULT_EOF) || (res == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
                REQUIRE(ok);
                break;
            }
            default:
                FAIL("Invalid start result");
                break;
            }

            break;
        }
    }
}

TEST_CASE("gcode-reader-empty-validity", "[GcodeReader]") {
    // Test the "empty validity" (which, implicitly has size = 0, even if the
    // file should be considered bigger) prevents reading from it even though
    // the ranges are capped. Basically, checking interaction of internal
    // implementation nuances don't change and the default State prevents
    // reading from the file no matter what.
    auto reader = AnyGcodeFormatReader(PLAIN_TEST_FILE);
    auto r = dynamic_cast<PlainGcodeReader *>(reader.get());
    REQUIRE(r != nullptr);

    r->set_validity(State {});
    GcodeBuffer buffer;
    REQUIRE(r->stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OUT_OF_RANGE);
}

TEST_CASE("File size estimate", "[GcodeReader]") {
    for (auto &test : test_files) {
        if (!test.has_plain_metadata) {
            // In this case, the metadata are part of the encripted section.
            // That throws the estimate off.
            //
            // However, we don't consider these to be fully supported, more
            // like, we provide a minimal ("at least prints the instructions
            // somehow") support. In that regard, having the estimate somewhat
            // off is OK and dealing with proper estimates will come with
            // "full" support of such gcodes.
            continue;
        }
        SECTION(std::string("Test-file: ") + test.filename) {
            auto reader = AnyGcodeFormatReader(test.filename);
            // Needed for the encrypted file, so that we do all the initial
            // asymmetric decryption stuff
            REQUIRE(reader->valid_for_print(true));
            auto estimate = reader.get()->get_gcode_stream_size_estimate();
            auto real = reader.get()->get_gcode_stream_size();
            float ratio = (float)estimate / real;
            std::cout << "Real: " << real << ", estimate: " << estimate << ", ratio: " << ratio << std::endl;
            REQUIRE_THAT(ratio, Catch::Matchers::WithinAbs(1, 0.1));
        }
    }
}

TEST_CASE("Reader: Long comment, split") {
    DummyReader reader(DUMMY_DATA_LONG, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == "; Short line");
    // Checking both, because len bases it on end-begin, strlen on \0 position
    REQUIRE(buffer.line.len() == 12);
    REQUIRE(strlen(buffer.line.c_str()) == 12);
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Long line01234567890123456789012345678901234567890123456789012345678901234567890");
    // Note: In the split mode, it is _not_ \0 terminated here.
    // Therefore, no strlen and using all 81 characters.
    REQUIRE(buffer.line.len() == 81);
    REQUIRE_FALSE(buffer.line_complete);

    // The continuation
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == "1234567890123456789012345678901234567890123456789");
    REQUIRE(buffer.line.len() == 49);
    REQUIRE(strlen(buffer.line.c_str()) == 49);
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Another short line");
    REQUIRE(buffer.line.len() == 19);
    REQUIRE(strlen(buffer.line.c_str()) == 19);
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Long comment, discard") {
    DummyReader reader(DUMMY_DATA_LONG, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == "; Short line");
    // Checking both, because len bases it on end-begin, strlen on \0 position
    REQUIRE(buffer.line.len() == 12);
    REQUIRE(strlen(buffer.line.c_str()) == 12);
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Long line0123456789012345678901234567890123456789012345678901234567890123456789");
    REQUIRE(buffer.line.len() == 80);
    REQUIRE(strlen(buffer.line.c_str()) == 80);
    REQUIRE_FALSE(buffer.line_complete);

    // The continuation is not present

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Another short line");
    REQUIRE(buffer.line.len() == 19);
    REQUIRE(strlen(buffer.line.c_str()) == 19);
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Exact long, split") {
    DummyReader reader(DUMMY_DATA_EXACT, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";01234567890123456789012345678901234567890123456789012345678901234567890123456789");
    REQUIRE(buffer.line.len() == 81);
    REQUIRE_FALSE(buffer.line_complete);

    // There's an empty continuation to mark it is complete
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line.is_empty());
    REQUIRE(buffer.line_complete);

    // Then the rest can be read
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Another line");
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Exact long, discard") {
    DummyReader reader(DUMMY_DATA_EXACT, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";0123456789012345678901234567890123456789012345678901234567890123456789012345678");
    REQUIRE(buffer.line.len() == 80);
    REQUIRE_FALSE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";Another line");
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Exact at EOF, split") {
    DummyReader reader(DUMMY_DATA_EXACT_EOF, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";01234567890123456789012345678901234567890123456789012345678901234567890123456789");
    REQUIRE(buffer.line.len() == 81);
    REQUIRE_FALSE(buffer.line_complete);

    // There's an empty continuation to mark it is complete
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line.is_empty());
    REQUIRE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Exact at EOF, discard") {
    DummyReader reader(DUMMY_DATA_EXACT_EOF, IGcodeReader::Result_t::RESULT_EOF);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";0123456789012345678901234567890123456789012345678901234567890123456789012345678");
    REQUIRE(buffer.line.len() == 80);
    REQUIRE_FALSE(buffer.line_complete);

    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_EOF);
}

TEST_CASE("Reader: Error in long, split") {
    DummyReader reader(DUMMY_DATA_ERR, IGcodeReader::Result_t::RESULT_ERROR);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Split) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";01234567890123456789012345678901234567890123456789012345678901234567890123456789");
    REQUIRE(buffer.line.len() == 81);
    REQUIRE_FALSE(buffer.line_complete);

    // Error reading the continuation.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_ERROR);
}

TEST_CASE("Reader: Error in long, discard") {
    DummyReader reader(DUMMY_DATA_ERR, IGcodeReader::Result_t::RESULT_ERROR);
    GcodeBuffer buffer;

    // The first line fits exactly. But the reader doesn't know it ended.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_OK);
    REQUIRE(buffer.line == ";0123456789012345678901234567890123456789012345678901234567890123456789012345678");
    REQUIRE(buffer.line.len() == 80);
    REQUIRE_FALSE(buffer.line_complete);

    // Interestingly, this is not when reading the end of the line, but reading
    // the next line.. but it still results in ERROR.
    REQUIRE(reader.stream_get_line(buffer, IGcodeReader::Continuations::Discard) == IGcodeReader::Result_t::RESULT_ERROR);
}

TEST_CASE("Reader CRC: incorrect before gcode") {
    AnyGcodeFormatReader reader("test_bad_crc_intro.bgcode");
    REQUIRE(reader.is_open());
    REQUIRE(reader.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_CORRUPT);
}

TEST_CASE("Reader CRC: incorrect on first gcode") {
    AnyGcodeFormatReader reader("test_bad_crc_first_gcode.bgcode");
    REQUIRE(reader.is_open());
    // The first gcode block is checked during the start
    REQUIRE(reader.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_CORRUPT);
}

TEST_CASE("Reader CRC: incorrect on another gcode") {
    AnyGcodeFormatReader reader("test_bad_crc_gcode.bgcode");
    REQUIRE(reader.is_open());
    // This checks only the beginning, not the whole gcode and so far we didn't find the "broken" part yet.
    REQUIRE(reader.get()->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);

    char buffer[128];
    IGcodeReader::Result_t result = IGcodeReader::Result_t::RESULT_OK;

    while (result == IGcodeReader::Result_t::RESULT_OK) {
        size_t size = sizeof buffer;
        result = stream_get_block(*reader, buffer, size);
    }

    // We finish by finding a corruption, not running until the very end.
    REQUIRE(result == IGcodeReader::Result_t::RESULT_CORRUPT);
}

TEST_CASE("Encrypted bgcode stream whole file") {
    AnyGcodeFormatReader enc_reader(BINARY_ENCRYPTED_CORRECT_GCODE);
    REQUIRE(enc_reader.is_open());
    REQUIRE(enc_reader->valid_for_print(true));
    AnyGcodeFormatReader dec_reader("test_decrypted_gcode_correct.bgcode");
    REQUIRE(dec_reader.is_open());
    REQUIRE(dec_reader->valid_for_print(true));
    IGcodeReader::Result_t enc_result;
    IGcodeReader::Result_t dec_result;

    SECTION("metadata") {
        REQUIRE(enc_reader->stream_metadata_start());
        REQUIRE(dec_reader->stream_metadata_start());
        GcodeBuffer enc_buffer;
        GcodeBuffer dec_buffer;
        dec_result = dec_reader->stream_get_line(dec_buffer, IGcodeReader::Continuations::Discard);
        while ((enc_result = enc_reader->stream_get_line(enc_buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
            REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_OK);
            REQUIRE(strcmp(enc_buffer.buffer.data(), dec_buffer.buffer.data()) == 0);
            dec_result = dec_reader->stream_get_line(dec_buffer, IGcodeReader::Continuations::Discard);
        }
        REQUIRE(enc_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
    }

    SECTION("gcode get_line") {
        REQUIRE(enc_reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
        REQUIRE(dec_reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
        GcodeBuffer enc_buffer;
        GcodeBuffer dec_buffer;
        dec_result = dec_reader->stream_get_line(dec_buffer, IGcodeReader::Continuations::Discard);
        while ((enc_result = enc_reader->stream_get_line(enc_buffer, IGcodeReader::Continuations::Discard)) == IGcodeReader::Result_t::RESULT_OK) {
            REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_OK);
            REQUIRE(strcmp(enc_buffer.buffer.data(), dec_buffer.buffer.data()) == 0);
            dec_result = dec_reader->stream_get_line(dec_buffer, IGcodeReader::Continuations::Discard);
        }
        REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        REQUIRE(enc_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
    }

    char enc_char;
    char dec_char;
    SECTION("gcode by chars") {
        REQUIRE(enc_reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
        REQUIRE(dec_reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
        dec_result = dec_reader->stream_getc(dec_char);
        while ((enc_result = enc_reader->stream_getc(enc_char)) == IGcodeReader::Result_t::RESULT_OK) {
            REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_OK);
            REQUIRE(dec_char == enc_char);
            dec_result = dec_reader->stream_getc(dec_char);
        }
        REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        REQUIRE(enc_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
    }

    SECTION("thumbnail") {
        REQUIRE(enc_reader->stream_thumbnail_start(16, 16, IGcodeReader::ImgType::PNG, true));
        REQUIRE(dec_reader->stream_thumbnail_start(16, 16, IGcodeReader::ImgType::PNG, true));
        dec_result = dec_reader->stream_getc(dec_char);
        while ((enc_result = enc_reader->stream_getc(enc_char)) == IGcodeReader::Result_t::RESULT_OK) {
            REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_OK);
            REQUIRE(dec_char == enc_char);
            dec_result = dec_reader->stream_getc(dec_char);
        }
        REQUIRE(dec_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
        REQUIRE(enc_result == IGcodeReader::Result_t::RESULT_EOF); // file was read fully without error
    }
}

TEST_CASE("Encrypted bgcode: wrong last block") {
    AnyGcodeFormatReader enc_reader;
    SECTION("early last block") {
        enc_reader = AnyGcodeFormatReader("test_encrypted_gcode_early_last_block.bgcode");
    }
    SECTION("no last block") {
        enc_reader = AnyGcodeFormatReader("test_encrypted_gcode_no_last_block.bgcode");
    }
    // test_encrypted_gcode_no_last_block.bgcode
    REQUIRE(enc_reader.is_open());
    REQUIRE(enc_reader->valid_for_print(true));
    REQUIRE(enc_reader->stream_gcode_start() == IGcodeReader::Result_t::RESULT_OK);
    IGcodeReader::Result_t enc_result;
    char c;
    while ((enc_result = enc_reader->stream_getc(c)) == IGcodeReader::Result_t::RESULT_OK) {
        enc_result = enc_reader->stream_getc(c);
    }
    REQUIRE(enc_result == IGcodeReader::Result_t::RESULT_CORRUPT);
}

TEST_CASE("Plain bgcode valid") {
    AnyGcodeFormatReader reader("test_binary_heatshrink.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE(reader->valid_for_print(false));
    }
    SECTION("full check") {
        REQUIRE(reader->valid_for_print(true));
    }
}

TEST_CASE("Encrypted bgcode valid") {
    AnyGcodeFormatReader reader(BINARY_ENCRYPTED_CORRECT_GCODE);
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE(reader->valid_for_print(false));
    }
    SECTION("full asymmetric check") {
        REQUIRE(reader->valid_for_print(true));
    }
}

TEST_CASE("Encrypted bgcode key for different printer") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_diff_printer.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_for_different_printer) == 0);
    }
    SECTION("ful check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_for_different_printer) == 0);
    }
}

TEST_CASE("Encrypted bgcode wrong key block hash") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_bad_key_hash.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE(reader->valid_for_print(false));
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::key_block_hash_mismatch) == 0);
    }
}

TEST_CASE("Encrypted bgcode metadata not at beggining") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_metadata_not_beggining.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::metadata_not_beggining) == 0);
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::metadata_not_beggining) == 0);
    }
}

TEST_CASE("Encrypted bgcode key block before identity") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_key_before_identity.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::key_before_identity) == 0);
    }
    SECTION("full check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::key_before_identity) == 0);
    }
}

TEST_CASE("Encrypted bgcode encrypted block before identity") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_encrypted_before_identity.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_before_identity) == 0);
    }
    SECTION("full check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_before_identity) == 0);
    }
}

TEST_CASE("Encrypted bgcode encrypted block before key") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_encrypted_before_key.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_before_key) == 0);
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::encrypted_before_key) == 0);
    }
}

TEST_CASE("Encrypted bgcode plain gcode block in encrypted block") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_plain_gcode_inside_encrypted.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::unencrypted_in_encrypted) == 0);
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::unencrypted_in_encrypted) == 0);
    }
}

TEST_CASE("Encrypted bgcode invalid identity key") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_invalid_identity_key.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE_FALSE(reader->valid_for_print(false));
        REQUIRE(strcmp(reader->error_str(), e2ee::identity_parsing_error) == 0);
    }
    SECTION("full check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::identity_parsing_error) == 0);
    }
}

TEST_CASE("Encrypted bgcode bad identity block signature") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_bad_identity_block_signature.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE(reader->valid_for_print(false));
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::identity_verification_fail) == 0);
    }
}

TEST_CASE("Encrypted bgcode corrupted metadata") {
    AnyGcodeFormatReader reader("test_encrypted_gcode_corrupted_metadata.bgcode");
    REQUIRE(reader.is_open());
    SECTION("basic check") {
        REQUIRE(reader->valid_for_print(false));
    }
    SECTION("full asymmetric check") {
        REQUIRE_FALSE(reader->valid_for_print(true));
        REQUIRE(strcmp(reader->error_str(), e2ee::corrupted_metadata) == 0);
    }
}
