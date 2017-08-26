#ifndef SEASTATE_H
#define SEASTATE_H

#include <cstdint>
#include <iostream>

class SeaState {
 public:
    SeaState() :
    a(0x16f11fe89b0d677cLLU),
    b(0xb480a793d8e6c86cLLU),
    c(0x6fe2e5aaf078ebc9LLU),
    d(0x14f994a4c5259381LLU),
    length(0) {
    }

    SeaState(uint64_t s1, uint64_t s2, uint64_t s3, uint64_t s4) :
    a(s1), b(s2), c(s3), d(s4), length(0) {
    }

    uint64_t readData(std::istream& s, uint64_t &i) const;
    uint64_t readData(const std::string& s, std::string::size_type& c, uint64_t& i) const;
    uint64_t hash(const std::string& s);
    uint64_t finalize() const;
    void update(uint64_t n, uint64_t len=8);

private:
    uint64_t f(uint64_t x) const;

    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    uint64_t length;
};

#endif
