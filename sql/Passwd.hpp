#pragma once

#include <array>
#include <iomanip>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class Password {
public:
    static std::string hash(const std::string& password) {
        constexpr int ITERATIONS = 210000;
        constexpr int SALT_SIZE = 16;
        constexpr int HASH_SIZE = 32;

        std::array<unsigned char, SALT_SIZE> salt{};
        std::array<unsigned char, HASH_SIZE> digest{};

        if (RAND_bytes(salt.data(), salt.size()) != 1) {
            throw std::runtime_error("RAND_bytes failed");
        }

        if (PKCS5_PBKDF2_HMAC(password.data(), password.size(), salt.data(), salt.size(),
                ITERATIONS, EVP_sha256(), digest.size(), digest.data()) != 1) {
            throw std::runtime_error("PBKDF2 failed");
        }

        return "pbkdf2_sha256$" +
               std::to_string(ITERATIONS) + "$" +
               hex(salt.data(), salt.size()) + "$" +
               hex(digest.data(), digest.size());
    }

    static bool verify(const std::string& password, const std::string& encoded) {
        std::vector<std::string> parts;
        split(encoded, '$', parts);

        if (parts.size() != 4) {
            return false;
        }

        if (parts[0] != "pbkdf2_sha256") {
            return false;
        }

        const int iterations = std::stoi(parts[1]);

        const auto salt = from_hex(parts[2]);
        const auto expected = from_hex(parts[3]);

        if (salt.empty() || expected.empty()) {
            return false;
        }

        std::vector<unsigned char> actual(expected.size());

        if (PKCS5_PBKDF2_HMAC(
                password.data(),
                password.size(),
                salt.data(),
                salt.size(),
                iterations,
                EVP_sha256(),
                actual.size(),
                actual.data()) != 1) {
            return false;
        }

        if (actual.size() != expected.size()) {
            return false;
        }

        unsigned char diff = 0;

        for (size_t i = 0; i < actual.size(); ++i) {
            diff |= actual[i] ^ expected[i];
        }

        return diff == 0;
    }

private:
    static std::string hex(const unsigned char* data, size_t size) {
        std::ostringstream oss;

        for (size_t i = 0; i < size; ++i) {
            oss << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<int>(data[i]);
        }

        return oss.str();
    }

    static std::vector<unsigned char> from_hex(const std::string& s) {
        if (s.size() % 2 != 0) {
            return {};
        }

        std::vector<unsigned char> result(s.size() / 2);

        for (size_t i = 0; i < result.size(); ++i) {
            unsigned int x;

            std::stringstream ss;
            ss << std::hex << s.substr(i * 2, 2);
            ss >> x;

            result[i] =
                static_cast<unsigned char>(x);
        }

        return result;
    }

    static void split(const std::string& s, char delim, std::vector<std::string>& result) {
        std::stringstream ss(s);
        std::string item;

        while (std::getline(ss, item, delim)) {
            result.push_back(item);
        }
    }
};