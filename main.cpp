/*
 * Halo 2 Color Plate Extractor
 * Copyright (c) Snowy Mouse 2021
 *
 * This software is licensed under version 3 of the GNU GPL as published by the Free Software Foundation in 2007.
 *
 * See COPYING for more information
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <string>
#include <vector>
#include <bit>
#include <zlib.h>
#include <tiffio.h>
#include <thread>
#include <list>
#include <mutex>
#include <atomic>
#include <chrono>

static std::mutex log_mutex;
static std::atomic<unsigned long> extracted_count;

#define DO_IN_MUTEX(...) { \
    log_mutex.lock(); \
    __VA_ARGS__; \
    log_mutex.unlock(); \
}

static int dump_single_bitmap(const std::filesystem::path &tags, const std::filesystem::path &data, const std::string &bitmap_tag_path, bool overwrite = false);

static void dump_single_bitmap_worker(const std::filesystem::path *tags, const std::filesystem::path *data, const std::string *bitmap_tag_path, std::mutex *mutex, bool overwrite) {
    dump_single_bitmap(*tags, *data, *bitmap_tag_path, overwrite);
    mutex->unlock();
}

int main(int argc, const char **argv) {
    if(argc != 4) {
        std::printf("Usage: %s <tags> <data> <tag-path|\"all\"|\"all-overwrite\">\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Get the paths. Make sure they exist
    std::filesystem::path tags = argv[1];
    std::filesystem::path data = argv[2];

    if(!std::filesystem::exists(tags)) {
        std::fprintf(stderr, "%s does not exist\n", tags.string().c_str());
        return EXIT_FAILURE;
    }

    if(!std::filesystem::exists(data)) {
        std::fprintf(stderr, "%s does not exist\n", data.string().c_str());
        return EXIT_FAILURE;
    }

    // If doing all, thread it
    if(std::strcmp(argv[3], "all") == 0 || std::strcmp(argv[3], "all-overwrite") == 0) {
        bool overwrite = std::strcmp(argv[3], "all-overwrite") == 0;
        extracted_count = 0;

        auto thread_count = std::thread::hardware_concurrency();
        if(thread_count == 0) {
            thread_count = 1;
        }

        std::vector<std::thread> threads(thread_count);
        std::vector<std::mutex> threads_mutex(thread_count);
        std::list<std::string> tag_paths;

        unsigned long potential_tag_count = 0;
        auto start = std::chrono::steady_clock::now();

        for(auto &i : std::filesystem::recursive_directory_iterator(tags, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied)) {
            if(i.is_regular_file()) {
                auto path = i.path();
                if(path.extension() == ".bitmap") {
                    while(true) {
                        bool found = false;
                        for(std::size_t t = 0; t < thread_count; t++) {
                            auto &mutex = threads_mutex[t];
                            auto &thread = threads[t];
                            if(mutex.try_lock()) {
                                if(thread.joinable()) {
                                    thread.join();
                                }
                                thread = std::thread(dump_single_bitmap_worker, &tags, &data, &tag_paths.emplace_back(path.lexically_relative(tags).string()), &mutex, overwrite);
                                potential_tag_count++;
                                found = true;
                                break;
                            }
                        }
                        if(found) {
                            break;
                        }
                    }
                }
            }
        }

        for(auto &m : threads_mutex) {
            m.lock();
        }

        for(auto &t : threads) {
            if(t.joinable()) {
                t.join();
            }
        }
        auto end = std::chrono::steady_clock::now();

        std::printf("Extracted %lu / %lu color plate%s in %.03f ms\n", extracted_count.load(), potential_tag_count, potential_tag_count == 1 ? "" : "s", std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0);

        return EXIT_SUCCESS;
    }

    // Otherwise just do the one bitmap
    else {
        return dump_single_bitmap(tags, data, argv[3]);
    }

    return EXIT_FAILURE;
}

static bool dump_bitmap_to_directory(const std::filesystem::path &tag, const std::filesystem::path &data, bool overwrite);

static int dump_single_bitmap(const std::filesystem::path &tags, const std::filesystem::path &data, const std::string &bitmap_tag_path, bool overwrite) {
    // Get the path
    std::string bitmap_path_str = bitmap_tag_path;
    for(char &c : bitmap_path_str) {
        if(c == '\\') {
            c = std::filesystem::path::preferred_separator;
        }
    }

    // Ensure it ends with ".bitmap"
    std::filesystem::path bitmap_path = bitmap_path_str;
    if(bitmap_path.extension() != ".bitmap") {
        DO_IN_MUTEX(std::fprintf(stderr, "%s does not end with .bitmap\n", bitmap_path.string().c_str()));
        return EXIT_FAILURE;
    }

    // Ensure it exists
    auto bitmap_file_path = tags / bitmap_path;
    if(!std::filesystem::exists(bitmap_file_path)) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s does not exist\n", bitmap_file_path.string().c_str()));
        return EXIT_FAILURE;
    }

    // Replace the extension
    auto bitmap_data_path = data / bitmap_path;
    bitmap_data_path.replace_extension(".tif");

    // Do it now
    if(dump_bitmap_to_directory(bitmap_file_path, bitmap_data_path, overwrite)) {
        DO_IN_MUTEX(std::printf("Extracted %s\n", bitmap_tag_path.c_str()));
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

template<typename T> static T swap_endian(T v) {
    std::uint8_t swapped[sizeof(v)];
    std::uint8_t *unswapped = reinterpret_cast<std::uint8_t *>(&v);

    for(std::size_t i = 0; i < sizeof(swapped); i++) {
        swapped[i] = unswapped[sizeof(T) - 1 - i];
    }

    return *reinterpret_cast<T *>(swapped);
}

template<typename T> static T little_to_native(T little) {
    if(std::endian::native == std::endian::little) {
        return little;
    }
    else {
        return swap_endian(little);
    }
}

template<typename T> static T big_to_native(T big) {
    if(std::endian::native == std::endian::big) {
        return big;
    }
    else {
        return swap_endian(big);
    }
}

static bool dump_bitmap_to_directory(const std::filesystem::path &tag, const std::filesystem::path &data, bool overwrite) {
    // First check if it exists
    auto bitmap_data_path_str = data.string();
    if(!overwrite && std::filesystem::exists(data)) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s already exists\n", bitmap_data_path_str.c_str()));
        return false;
    }

    // Open it
    auto tag_str = tag.string();
    FILE *f = std::fopen(tag.string().c_str(), "rb");
    if(!f) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be opened for reading\n", tag_str.c_str()));
        return false;
    }

    // Size?
    std::fseek(f, 0, SEEK_END);
    auto size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    // Allocate it
    std::vector<std::byte> tag_data;
    try {
        tag_data.resize(size);
    }
    catch(std::exception &) {
        std::fclose(f);
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be read due to not enough memory\n", tag_str.c_str()));
        return false;
    }
    auto *tag_data_data = tag_data.data();

    // Read it
    if(!std::fread(tag_data_data, size, 1, f)) {
        std::fclose(f);
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be read\n", tag_str.c_str()));
        return false;
    }

    // Done with that
    std::fclose(f);
    f = nullptr;

    // Bad!
    if(size < 0x100 || little_to_native(*reinterpret_cast<std::uint32_t *>(tag_data_data + 0x24)) != 'bitm' || little_to_native(*reinterpret_cast<std::uint16_t *>(tag_data_data + 0x38) != 7)) {
        (std::fprintf(stderr, "%s is not a valid bitmap tag\n", tag_str.c_str()));
        return false;
    }

    // Get this color plate information
    std::uint32_t color_plate_width = little_to_native(*reinterpret_cast<std::uint16_t *>(tag_data_data + 0x68));
    std::uint32_t color_plate_height = little_to_native(*reinterpret_cast<std::uint16_t *>(tag_data_data + 0x6A));
    auto color_plate_compressed_length = little_to_native(*reinterpret_cast<std::uint32_t *>(tag_data_data + 0x6C));

    // Skip if we don't have color plate
    if(color_plate_compressed_length == 0) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s has no color plate data\n", tag_str.c_str()));
        return false;
    }

    // Can we read it?
    std::size_t compressed_color_plate_offset = little_to_native(*reinterpret_cast<std::uint32_t *>(tag_data_data + 0x4C)) + 0x50;

    if(color_plate_compressed_length + compressed_color_plate_offset > size || color_plate_compressed_length < sizeof(std::uint32_t)) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s is corrupt (%u + %u > %zu || %zu < 4)\n", tag_str.c_str(), color_plate_compressed_length, compressed_color_plate_offset, size, color_plate_compressed_length));
        return false;
    }

    // Let's try
    std::vector<std::uint32_t> decompressed_color_plate_data;
    auto *compressed_color_plate_data = tag_data_data + compressed_color_plate_offset + sizeof(std::uint32_t);
    std::size_t color_plate_size = big_to_native(*reinterpret_cast<std::uint32_t *>(compressed_color_plate_data - sizeof(std::uint32_t)));

    // But first make sure it's not bullshit
    if(color_plate_width * color_plate_height * sizeof(std::uint32_t) != color_plate_size) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s has invalid color plate data (%u x %u x 4 != %zu)\n", tag_str.c_str(), color_plate_width, color_plate_height, color_plate_size));
        return false;
    }

    // Resize it
    try {
        decompressed_color_plate_data.resize(color_plate_size / sizeof(std::uint32_t));
    }
    catch(std::exception &) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be decompressed due to not enough memory\n", tag_str.c_str()));
        return false;
    }

    // Now let's decompress that
    z_stream inflate_stream;
    inflate_stream.zalloc = Z_NULL;
    inflate_stream.zfree = Z_NULL;
    inflate_stream.opaque = Z_NULL;
    inflate_stream.avail_out = color_plate_size;
    inflate_stream.next_out = reinterpret_cast<Bytef *>(decompressed_color_plate_data.data());
    inflate_stream.avail_in = color_plate_compressed_length - sizeof(std::uint32_t);
    inflate_stream.next_in = reinterpret_cast<Bytef *>(compressed_color_plate_data);
    
    // Do it
    if(inflateInit(&inflate_stream) != Z_OK || inflate(&inflate_stream, Z_FINISH) != Z_STREAM_END || inflateEnd(&inflate_stream) != Z_OK) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be decompressed due to invalid compressed data\n", tag_str.c_str()));
        return false;
    }

    // Now that that's done, let's make some directories
    auto data_parent = data.parent_path();

    try {
        std::filesystem::create_directories(data_parent);
    }
    catch(std::exception &e) {
        DO_IN_MUTEX(std::fprintf(stderr, "Directory %s could not be made\n", data_parent.string().c_str()));
        return false;
    }

    // Make the TIF
    auto *tiff = TIFFOpen(bitmap_data_path_str.c_str(), "w");
    if(!tiff) {
        DO_IN_MUTEX(std::fprintf(stderr, "%s could not be opened for writing\n", bitmap_data_path_str.c_str()));
        return false;
    }

    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, color_plate_width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, color_plate_height);

    std::uint16_t extrasamples = EXTRASAMPLE_UNASSALPHA;
    TIFFSetField(tiff, TIFFTAG_EXTRASAMPLES, 1, &extrasamples);

    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 4);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8, 8, 8, 8);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);

    // Swap endianness
    for(auto &i : decompressed_color_plate_data) {
        i = little_to_native(i);
    }

    // Swap blue and red channels
    for(auto &i : decompressed_color_plate_data) {
        i = (i & 0xFF00FF00) | ((i & 0x00FF0000) >> 16) | ((i & 0x000000FF) << 16);
    }

    // Write it
    for (std::size_t y = 0; y < color_plate_height; y++) {
        TIFFWriteScanline(tiff, decompressed_color_plate_data.data() + y * color_plate_width, y, 0);
    }

    TIFFClose(tiff);

    extracted_count++;

    // Done!
    return true;
}